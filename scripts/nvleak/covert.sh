#!/bin/bash

set -e

batch_id="$(date +%Y%m%d%H%M%S)"

script_root=$(realpath $(realpath $(dirname $0)))
export no_slack=y
source "$script_root/utils/slack.sh"
export SlackURL=https://hooks.slack.com/services/***/***/******

source "$script_root/utils/remake.sh"
source "$script_root/utils/search_backend_dev.sh"
source "$script_root/utils/profiler_none.sh"

prepare() {
	echo "Set host CPU into performance mode"
	for line in $(find /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor); do
		echo "Set CPU [$line] into performance mode"
		echo "performance" >"$line"
	done
	# Disable cache prefetcher
	echo "Disable cache prefetcher"
	wrmsr -a 0x1a4 0xf
	# Load profiler
	load_profiler
	TaskDir=$PWD \
	start_profiler
}

cleanup() {
	# Unload profiler
	stop_profiler || true
	unload_profiler
	# Enable cache prefetcher
	echo "Re-enable cache prefetcher"
	wrmsr -a 0x1a4 0x0
}

trap cleanup EXIT

covert_fid_array=(
	6 7
	10 11
	22
)
block_size=64
stride_array=(
	# $((2 ** 16))
	# $((2 ** 17))
	# $((2 ** 18))
	# $((2 ** 19))
	$((2 ** 20)) # 2 ** 20 --> 1 MiB == 16MiB/16Way
	$((2 ** 21))
	$((2 ** 22))
	# $((2 ** 23))
	# $((2 ** 24))
)
region_array=(
	# $((2 **  7))
	# $((2 **  8))
	$((2 **  9))
	$(seq -s ' ' $((2 ** 9)) $((2 ** 6)) $((2 ** 10 - 1)))
	$((2 ** 10)) # 16 * 64 --> 16 blocks --> 16 way
	$((2 ** 11))
	$((2 ** 12))
	# $((2 ** 13))
)
sub_op_array=(1) # Covert channel: Pointer chasing read only
repeat_array=(4 8 16 32)
region_align=4096

fence_strategy_array=(0)
fence_freq_array=(1)
flush_after_load_array=(1)
record_timing_array=(1) # 1: per_reapeat
flush_l1_array=(0 1)
receiver_page_offset_array=(0 1)

covert_chasing_store=0
covert_chasing_load=1
# iter_cycle_ddl_base=70000 # auto detected

function run_qemu() {
	# $1 sender | receiver
	# $@ other args
	core_id=1
	case $1 in
		sender)
			core_id="4-7"
		;;
		receiver)
			core_id="8-11"
		;;
		*)
		echo "Wrong usage"
		echo "Usage: run_qemu sender|receiver [... args]"
		;;
	esac

	use_tmux=0 set_cpu_perf_mode=0 dump_covert_data=0 \
	nice -n -19 \
	numactl --physcpubind "${core_id}" \
		"$script_root/qemu_nvram.sh" "$@"
}

function bench_func_inner() {
	mkdir -p "${batch_result_dir}"

	prepare

	for repeat in "${repeat_array[@]}"; do
		for flush_l1 in "${flush_l1_array[@]}"; do
			for record_timing in "${record_timing_array[@]}"; do
				for flush_after_load in "${flush_after_load_array[@]}"; do
					for sub_op in "${sub_op_array[@]}"; do
						for fence_strategy in "${fence_strategy_array[@]}"; do
							for fence_freq in "${fence_freq_array[@]}"; do
								for covert_fid in "${covert_fid_array[@]}"; do
									# See following:
									#   - src/Makefile
									#   - src/microbench/chasing.h
									NVLEAK_MAKE_ARGS=$(cat <<- EOF
										-DCHASING_FENCE_STRATEGY_ID="$fence_strategy" \
										-DCHASING_FENCE_FREQ_ID="$fence_freq" \
										-DCHASING_FLUSH_AFTER_LOAD="$flush_after_load" \
										-DCHASING_RECORD_TIMING="$record_timing" \
										-DCHASING_FLUSH_L1="$flush_l1" \
										-DCOVERT_CHASING_STORE="$covert_chasing_store" \
										-DCOVERT_CHASING_LOAD="$covert_chasing_load"
									EOF
									)
									NVLEAK_MAKE_ARGS="$(echo "$NVLEAK_MAKE_ARGS" | tr -s '\t')"
									export NVLEAK_MAKE_ARGS

									remake_nvleak

									# Discover covert data file and dump into dax dev
									echo "Dump covert data to the backend file"
									dumper="$(realpath "${script_root}/dump")"
									gcc "${dumper}.c" -o "${dumper}" || exit 1
									echo "Covert data file id: ${covert_fid}"
									sender_backend_dev="$(search_backend_dev sender)"
									echo "Dumping to sender dev: ${sender_backend_dev}"
									covert_data_path="$(realpath "$(dirname "${0}")"/covert_data)"
									covert_data_file_pattern="${covert_data_path}/${covert_fid}.*.*.bin"
									if ls ${covert_data_file_pattern} 1>/dev/null 2>&1; then
										covert_data_file="$(readlink -f ${covert_data_file_pattern})"
										file_size_byte="$(stat --format=%s "${covert_data_file}")"
										${dumper} -o "${sender_backend_dev}" -i "${covert_data_file}" -s "${file_size_byte}"
										covert_data_bits=$((file_size_byte * 8))
									else
										echo "Cannot find covert data file with ID: ${covert_fid}"
										break
									fi

									SLACK_MSG=$(cat <<- EOF
										[Start   ] $(basename "$0")
											[FenceStrategy=$fence_strategy]
											[FenceFreq=$fence_freq]
											[FlushAfterLoad=$flush_after_load]
											[Repeat=$repeat]
											[RecordTiming=$record_timing]
											[SubOP=$sub_op]
											[RegionAlign=$region_align]
									EOF
									)

									slack_notice $SlackURL "$SLACK_MSG"
									iter=0
									for receiver_page_offset in "${receiver_page_offset_array[@]}"; do
									for region_size in "${region_array[@]}"; do
										for stride_size in "${stride_array[@]}"; do
											if ((region_size % block_size != 0)); then
												continue
											fi
											if ((region_size * stride_size / block_size + region_align >= $((2 ** 29)))); then
												continue
											fi
											
											iter_cycle_ddl_base=$((region_size * 15))

											task_id="$(date +%Y%m%d%H%M%S)-$(git rev-parse --short HEAD)-${TaskID:-$(hostname)}"
											task_results_dir="${batch_result_dir}/${task_id}"
											mkdir -p "${task_results_dir}"
											{
												# receiver
												run_qemu \
													"receiver" \
													"$covert_data_bits" \
													"$region_size" \
													"$block_size" \
													"$stride_size" \
													"$repeat" \
													"$region_align" \
													"$receiver_page_offset" \
													"$covert_fid" \
													"$((repeat * iter_cycle_ddl_base))" \
													> >(tee -a "${task_results_dir}/receiver.log" > /dev/null) \
													2>&1 \
												&
												receiver_id=$!

												# sender
												run_qemu \
													"sender" \
													"$covert_data_bits" \
													"$region_size" \
													"$block_size" \
													"$stride_size" \
													"$repeat" \
													"$region_align" \
													"$receiver_page_offset" \
													"$covert_fid" \
													"$((repeat * iter_cycle_ddl_base))" \
													> >(tee -a "${task_results_dir}/sender.log" > /dev/null) \
													2>&1 \
												&
												sender_id=$!

												echo "Receiver PID [$receiver_id] Sender PID [$sender_id]"
												wait $receiver_id
												wait $sender_id
											}
										done
										iter=$((iter + 1))
										progress=$(bc -l <<<"scale=2; 100 * $iter / ${#region_array[@]} / ${#receiver_page_offset_array[@]}")
										slack_notice $SlackURL "[Progress] $progress% [$iter / ${#region_array[@]} / ${#receiver_page_offset_array[@]}]"
									done
									done
									slack_notice $SlackURL "[End     ] $(basename "$0")"
								done
							done
						done
					done
				done
			done
		done
	done

	cleanup

	slack_notice $SlackURL "[Finish  ] check results"
}

function bench_func() {
	mkdir -p "${batch_result_dir}"
	{
		bench_func_inner
	}    > >(ts '[%Y-%m-%d %H:%M:%S]' | tee -a "$batch_result_dir/stdout.log")    \
 		2> >(ts '[%Y-%m-%d %H:%M:%S]' | tee -a "$batch_result_dir/stderr.log" >&2)
}

job=$1
case $job in
debug)
	region_array=($((2 ** 11)))
	stride_array=($((2 ** 20)))
	flush_l1_array=(1)
	repeat_array=(1)
	iter_cycle_ddl_base=$((50000))
	flush_after_load_array=(1)
	receiver_page_offset_array=(0 1 2 3)
	covert_fid_array=(2)
	export no_slack=1
	batch_result_dir="results/${job}/${batch_id}"
	bench_func
	;;
debug_single)
	region_array=($((14 * 64)))
	stride_array=($((2 ** 21)))
	flush_l1_array=(1)
	repeat_array=(32)
	iter_cycle_ddl_base=$((40000))
	flush_after_load_array=(1)
	receiver_page_offset_array=(0)
	covert_fid_array=(2)
	export no_slack=1
	batch_result_dir="results/${job}/${batch_id}"
	bench_func
	;;
debug_small)
	region_array=($((14 * 64)))
	stride_array=($((2 ** 21)))
	flush_l1_array=(1)
	repeat_array=(32)
	covert_chasing_store=0
	covert_chasing_load=1
	# iter_cycle_ddl_base=$((1))
	iter_cycle_ddl_base=$((40000))
	flush_after_load_array=(1)
	# receiver_page_offset_array=(1)
	receiver_page_offset_array=($(seq -s ' ' 0 7))
	covert_fid_array=(2)
	export no_slack=1
	batch_result_dir="results/${job}/${batch_id}"
	bench_func
	;;
all)
	batch_result_dir="results/${job}/${batch_id}"
	bench_func
	;;
all_small)
	batch_result_dir="results/${job}/${batch_id}"
	receiver_page_offset_array=($(seq -s ' ' 0 15))
	bench_func
	;;
all_small_same_devdax)
	echo "ERROR: Check receiver address offset before running"
	exit 1
	batch_result_dir="results/${job}/${batch_id}"
	receiver_page_offset_array=($(seq -s ' ' 0 15))
	covert_fid_array=(2)
	export same_devdax="sender"
	bench_func
	;;
*)
	echo "Error usage: ${job}"
	echo "Usage: $0 debug|all|all_small"
	;;
esac
