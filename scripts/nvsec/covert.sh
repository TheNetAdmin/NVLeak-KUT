#!/bin/bash

batch_id="$(date +%Y%m%d%H%M%S)"
batch_result_dir="results/${batch_id}"

mkdir -p "${batch_result_dir}"

script_root=$(realpath $(realpath $(dirname $0))/)
source "$script_root/utils/slack.sh"
export SlackURL=https://hooks.slack.com/services/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

source "$script_root/utils/remake.sh"
source "$script_root/utils/search_backend_dev.sh"

prepare() {
	echo "Set host CPU into performance mode"
	for line in $(find /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor); do
		echo "performance" >"$line"
	done
	# Disable cache prefetcher
	echo "Disable cache prefetcher"
	wrmsr -a 0x1a4 0xf
}

cleanup() {
	# Enable cache prefetcher
	echo "Re-enable cache prefetcher"
	wrmsr -a 0x1a4 0x0
}

covert_fid_array=(
	10
	11
)
block_size=64
stride_array=(
	# $((2 ** 16))
	# $((2 ** 17))
	$((2 ** 18))
	$((2 ** 19))
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
	# $(seq -s ' ' $((2 ** 9)) $((2 ** 6)) $((2 ** 10 - 1)))
	$((2 ** 10)) # 16 * 64 --> 16 blocks --> 16 way
	$((2 ** 11))
	$((2 ** 12))
	$((2 ** 13))
)
sub_op_array=(1) # Covert channel: Pointer chasing read only
repeat_array=(4 8 16 32)
region_align=4096

fence_strategy_array=(0)
fence_freq_array=(1)
flush_after_load_array=(0)
record_timing_array=(1) # 1: per_reapeat
flush_l1_array=(0 1)


function run_qemu() {
	# $1 sender | receiver
	# $@ other args
	use_tmux=0 set_cpu_perf_mode=0 dump_covert_data=0 \
	"$script_root/qemu_nvram.sh" "$@"
}

function bench_func() {
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
									NVSEC_MAKE_ARGS=$(cat <<- EOF
										-DCHASING_FENCE_STRATEGY_ID=$fence_strategy \
										-DCHASING_FENCE_FREQ_ID=$fence_freq \
										-DCHASING_FLUSH_AFTER_LOAD=$flush_after_load \
										-DCHASING_RECORD_TIMING=$record_timing \
										-DCHASING_FLUSH_L1=$flush_l1
									EOF
									)
									NVSEC_MAKE_ARGS=$(echo "$NVSEC_MAKE_ARGS" | tr -s '\t')
									export NVSEC_MAKE_ARGS

									remake_nvsec

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
									for region_size in "${region_array[@]}"; do
										for stride_size in "${stride_array[@]}"; do
											if ((region_size % block_size != 0)); then
												continue
											fi
											if ((region_size * stride_size / block_size + region_align >= $((2 ** 29)))); then
												continue
											fi
											
											task_id="$(date +%Y%m%d%H%M%S)-$(git rev-parse --short HEAD)-${TaskID:-$(hostname)}"
											task_results_dir="${batch_result_dir}/${task_id}"
											mkdir -p "${task_results_dir}"
											{
												# sender
												run_qemu \
													"sender" \
													"$covert_data_bits" \
													"$region_size" \
													"$block_size" \
													"$stride_size" \
													"$repeat" \
													"$region_align" \
													> "${task_results_dir}/sender.log" \
												&

												# receiver
												run_qemu \
													"receiver" \
													"$covert_data_bits" \
													"$region_size" \
													"$block_size" \
													"$stride_size" \
													"$repeat" \
													"$region_align" \
													> "${task_results_dir}/receiver.log" \
												&

												wait
											}
										done
										iter=$((iter + 1))
										progress=$(bc -l <<<"scale=2; 100 * $iter / ${#region_array[@]}")
										slack_notice $SlackURL "[Progress] $progress% [$iter / ${#region_array[@]}]"
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

	slack_notice $SlackURL "[Finish  ] <@U01QVMG14HH> check results"
}

job=$1
case $job in
debug)
	region_array=(256)
	stride_array=(256)
	flush_l1_array=(1)
	repeat_array=(1)
	covert_fid_array=(1)
	bench_func
	;;
all)
	bench_func
	;;
*)
	echo "Error usage: ${job}"
	echo "Usage: $0 debug|all"
	;;
esac
