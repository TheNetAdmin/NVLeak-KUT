#!/bin/bash

set -e
set -x

batch_id="$(date +%Y%m%d%H%M%S)"

script_root=$(realpath $(realpath $(dirname $0)))
source "$script_root/utils/slack.sh"
export SlackURL=https://hooks.slack.com/services/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

source "$script_root/utils/remake.sh"
source "$script_root/utils/search_backend_dev.sh"
source "$script_root/utils/profiler_aepwatch.sh"

prepare() {
	echo "Set host CPU into performance mode"
	for line in $(find /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor); do
		echo "performance" >"$line"
	done
	# Disable cache prefetcher
	echo "Disable cache prefetcher"
	wrmsr -a 0x1a4 0xf
	# Load profiler
	load_profiler
	print_profiler
}

cleanup() {
	# Enable cache prefetcher
	echo "Re-enable cache prefetcher"
	wrmsr -a 0x1a4 0x0
	# Unload profiler
	stop_profiler || true
	unload_profiler
}

trap cleanup EXIT

# Not used by vanilla pointer chasing
covert_fid_array=(
	UselessValue
)
block_size=64
stride_array=(
    # RMB Buffer 16KB --> 256B Entry Size --> 16KB/256B = 64 Entries
    # 16KB / 1 or 2 or 4 or 8 or 16 or ... 64
    # The below AIT Buffer covers 16KB/1 and 16KB/2 and 16KB/4
	$((2 ** 6))
    $((2 ** 8))
    $(seq -s ' ' $((2 ** 9)) $((2 ** 9)) $((2 ** 12 - 1))) # [512B,    4KB) per  512B -> 7

    # AIT Buffer 16MB --> 4KB Entry Size --> 16MB/4KB = 4096 Entries
    # 16MB / 1 or 2 or 4 or 8 or 16 or ... 4096
    # NOTE: latency turn point at:
    #       128KB
    #       2MB
    #       8MB
    $((2 ** 12)) $((2 ** 13))
    $(seq -s ' ' $((2 ** 14)) $((2 ** 14)) $((2 ** 16 - 1))) # [ 16KB,  64KB) per  16KB ->  3
    $(seq -s ' ' $((2 ** 16)) $((2 ** 16)) $((2 ** 18 - 1))) # [ 64KB, 256KB) per  64KB ->  3
    $(seq -s ' ' $((2 ** 18)) $((2 ** 18)) $((2 ** 20 - 1))) # [256KB,   1MB) per 256KB ->  3
    $(seq -s ' ' $((2 ** 20)) $((2 ** 20)) $((2 ** 22 - 1))) # [  1MB,   4MB) per   1MB ->  3
    $(seq -s ' ' $((2 ** 22)) $((2 ** 22)) $((2 ** 24 - 1))) # [  4MB,  16MB) per   4MB ->  3
    $(seq -s ' ' $((2 ** 24)) $((2 ** 24)) $((2 ** 26 - 1))) # [ 16MB,  64MB) per  16MB ->  3
)
##If region_skip == 512MB and stride_size == 64MB
# -> region_size / block_size * stride_size == 512MB
# -> region_size = 512MB / 64MB * 8
# -> region_size = 64 = 2**6
##If region_skip == 128GB and stride_size == 64MB
# -> region_size / block_size * stride_size == 128GB
# -> region_size = 128GB / 64MB * 8
# -> region_size = 16K = 16*1024 = 2**14
##region == 2**13 --> 8182 --> more than 4096 entries in ait buffer
region_array=(
    $(seq -s ' ' $((2 **  6)) $((2 ** 6)) $((2 ** 11 - 1)))  # [  64B,   2KB) per   64B -> 31
    $(seq -s ' ' $((2 ** 11)) $((2 ** 8)) $((2 ** 13 - 1)))  # [  2KB,   8KB) per  256B -> 15
    $(seq -s ' ' $((2 ** 13)) $((2 ** 10)) $((2 ** 14 - 1))) # [  8KB,  16KB) per  256B -> 31
    $(seq -s ' ' $((2 ** 14)) $((2 ** 13)) $((2 ** 16 - 1))) # [ 16KB,  64KB) per   8KB ->  6
    $(seq -s ' ' $((2 ** 16)) $((2 ** 14)) $((2 ** 19 - 1))) # [ 64KB, 512KB) per  32KB -> 15
    $(seq -s ' ' $((2 ** 19)) $((2 ** 16)) $((2 ** 20 - 1))) # [512KB,   1MB) per  64KB ->  7
    $((2 ** 20)) $((2 ** 21)) $((2 ** 22)) $((2 ** 23))
	$((2 ** 24)) $((2 ** 25)) $((2 ** 26)) $((2 ** 27))
)
repeat_array=(32)
region_align=4096

fence_strategy_array=(0)
fence_freq_array=(1)
flush_after_load_array=(1)
record_timing_array=(1) # 1: per_reapeat
flush_l1_array=(1)

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
		vanilla)
			core_id="4-7"
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
						for fence_strategy in "${fence_strategy_array[@]}"; do
							for fence_freq in "${fence_freq_array[@]}"; do
								for covert_fid in "${covert_fid_array[@]}"; do
									# See following:
									#   - src/Makefile
									#   - src/microbench/chasing.h
									NVSEC_MAKE_ARGS=$(cat <<- EOF
										-DCHASING_FENCE_STRATEGY_ID="$fence_strategy" \
										-DCHASING_FENCE_FREQ_ID="$fence_freq" \
										-DCHASING_FLUSH_AFTER_LOAD="$flush_after_load" \
										-DCHASING_RECORD_TIMING="$record_timing" \
										-DCHASING_FLUSH_L1="$flush_l1"
									EOF
									)
									NVSEC_MAKE_ARGS="$(echo "$NVSEC_MAKE_ARGS" | tr -s '\t')"
									export NVSEC_MAKE_ARGS

									remake_nvsec

									SLACK_MSG=$(cat <<- EOF
										[Start   ] $(basename "$0")
											[FenceStrategy=$fence_strategy]
											[FenceFreq=$fence_freq]
											[FlushAfterLoad=$flush_after_load]
											[Repeat=$repeat]
											[RecordTiming=$record_timing]
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
											if ((region_size * stride_size / block_size + region_align >= $((2 ** 32)))); then
												continue
											fi
											
											task_id="$(date +%Y%m%d%H%M%S)-$(git rev-parse --short HEAD)-${TaskID:-$(hostname)}"
											task_results_dir="${batch_result_dir}/${task_id}"
											mkdir -p "${task_results_dir}"
											
											start_profiler

											run_qemu \
												"vanilla" \
												"87654321" \
												"$region_size" \
												"$block_size" \
												"$stride_size" \
												"$repeat" \
												"$region_align" \
												"12345678" \
												"$covert_fid" \
												"1" \
												> >(tee -a "${task_results_dir}/stdout.log" > /dev/null) \
												2>&1 \
											;

											stop_profiler
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

	cleanup

	slack_notice $SlackURL "[Finish  ] <@U01QVMG14HH> check results"
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
	region_array=(64)
	stride_array=(256)
	flush_l1_array=(0 1)
	export no_slack=1
	batch_result_dir="results/vanilla/${job}/${batch_id}"
	bench_func
	;;
debug_single)
	region_array=(
		$((2 ** 12))
	)
	stride_array=(64)
	flush_l1_array=(1)
	export no_slack=1
	batch_result_dir="results/vanilla/${job}/${batch_id}"
	bench_func
	;;
debug_small)
	region_array=(
		$((2 **  8)) $((2 **  9)) $((2 ** 10)) $((2 ** 11))
		$((2 ** 12)) $((2 ** 13)) $((2 ** 14)) $((2 ** 15))
		$((2 ** 16)) $((2 ** 17)) $((2 ** 18)) $((2 ** 19))
		$((2 ** 20)) $((2 ** 21)) $((2 ** 22)) $((2 ** 23))
		$((2 ** 24)) $((2 ** 25)) $((2 ** 26)) $((2 ** 27))
	)
	# stride_array=(64)
	stride_array=(
		$((2 **  6)) $((2 **  7)) $((2 **  8)) $((2 **  9))
		$((2 ** 10)) $((2 ** 11)) $((2 ** 12)) $((2 ** 13))
		$((2 ** 14)) $((2 ** 15)) $((2 ** 16)) $((2 ** 17))
		$((2 ** 18)) $((2 ** 19)) $((2 ** 20)) $((2 ** 21))
		$((2 ** 22)) $((2 ** 23)) 
	)
	flush_l1_array=(1)
	export no_slack=1
	batch_result_dir="results/vanilla/${job}/${batch_id}"
	bench_func
	;;
all)
	batch_result_dir="results/vanilla/${job}/${batch_id}"
	bench_func
	;;
*)
	echo "Error usage: ${job}"
	echo "Usage: $0 debug|all"
	;;
esac
