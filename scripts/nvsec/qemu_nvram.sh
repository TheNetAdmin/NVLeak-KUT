#!/bin/bash
curr_path="$(realpath "$(dirname "${0}")")"

use_tmux="${use_tmux:-1}"
covert_data_file_id=${covert_data_file_id:-1}
set_cpu_perf_mode=${set_cpu_perf_mode:-1}
dump_covert_data=${dump_covert_data:-1}

usage() {
	echo "Usage: $0 [sender|receiver|vanilla] [other args for nvsec_covert]"
}

print_envs() {
	echo "Envs:"
	echo "    - covert_data_file_id: ${covert_data_file_id}"
	echo "    - use_tmux: ${use_tmux}"
	echo "    - set_cpu_perf_mode: ${set_cpu_perf_mode}"
	echo "    - dump_covert_data: ${dump_covert_data}"
}

# Arguments
if [ $# -eq 0 ]; then
	usage
	exit 1
fi

label="${1}"
debug="${debug:-off}"

case "${label}" in
	sender)
		# Create sender
	;;
	receiver)
		# Create receiver
	;;
	vanilla)
		# Vanilla pointer chasing
	;;
	*)
		usage
		exit 1
	;;
esac

# Print envs
print_envs

# Build kernel
kernel_file="nvram_covert.flat"
kernel_path="${curr_path}/../../x86/${kernel_file}"
kernel_path="$(realpath "${kernel_path}")"

pushd "$(dirname "${kernel_path}")/../" || exit 1
make -j 10 "x86/${kernel_file}" || exit 1
popd || exit 1

# # Check if PMEM is mounted
# echo "Check if PMEM is mounted"
# dax_mnt="/mnt/dax"
# dax_mounted="$(mount -v | grep -c "${dax_mnt}")"
# if [ "${dax_mounted}" -ne 1 ]; then
# 	echo "Please check if pmem is mounted at ${dax_mnt}"
# 	exit 1
# fi

# QEMU NVDIMM backends

echo "Searching NVDIMM backend device"
source "${curr_path}/utils/search_backend_dev.sh"
backend_dev="$(search_backend_dev "${label}")"

echo "Backend dev: ${backend_dev}"

# RAM configs
ram_size="1G"
ram_slots="2"     # >= number of ram + number of nvram
ram_max_size="5G" # >= $ram_size + $nvram_size

# QEMU path
export QEMU="../qemu/build/x86_64-softmmu/qemu-system-x86_64"

# QEMU configs
nvram_size="4G"
nvram_align_size="1G"

# QEMU args
qemu_comm_args=""
qemu_comm_args+=" -cpu max,+sse2,+ssse3,+sse4.1,+sse4.2,+avx,+avx2"
qemu_comm_args+=" -machine pc,nvdimm,accel=kvm"
qemu_comm_args+=" -m ${ram_size},slots=${ram_slots},maxmem=${ram_max_size}"
qemu_comm_args+=" -object memory-backend-file,id=mem1,share=on,mem-path=${backend_dev},size=${nvram_size},align=${nvram_align_size}"
qemu_comm_args+=" -device nvdimm,id=nvdimm1,memdev=mem1"

## NVDIMM as separate numa node
# qemu_comm_args+=" -numa node,nodeid=0,mem=1G"
# qemu_comm_args+=" -numa node,nodeid=1,mem=0"
# qemu_comm_args+=" -device nvdimm,id=nvdimm1,memdev=mem1,node=1"

if [ "${debug}" == "off" ]; then
	qemu_command="x86/run ${kernel_path}"
	qemu_command+=" ${qemu_comm_args}"
else
	qemu_command="qemu-system-x86_64"
	qemu_command+=" --no-reboot -vnc none -curses -net none -monitor telnet:127.0.0.1:1234,server,nowait"
	qemu_command+=" ${qemu_comm_args}"
fi

if (( dump_covert_data == 1 )); then
	echo "Dump covert data to the backend file"
	dumper="$(realpath "${curr_path}/dump")"
	gcc "${dumper}.c" -o "${dumper}" || exit 1

	echo "Covert data file id: ${covert_data_file_id}"
	covert_data_path="$(realpath "$(dirname "${0}")"/covert_data)"
	covert_data_file_pattern="${covert_data_path}/${covert_data_file_id}.*.*.bin"
	if ls ${covert_data_file_pattern} 1>/dev/null 2>&1; then
		covert_data_file="$(readlink -f ${covert_data_file_pattern})"
		file_size_byte="$(stat --format=%s "${covert_data_file}")"
		${dumper} -o "${backend_dev}" -i "${covert_data_file}" -s "${file_size_byte}"
		# dd if="${covert_data_file}" of=${rep_dev} bs=${file_size_byte} count=1 conv=fsync
		covert_data_bits=$((file_size_byte * 8))
	else
		echo "Cannot find covert data file with ID: ${covert_data_file_id}"
		exit 2
	fi
fi

# ${dumper} -f "${backend_dev}" -d 0xcccccccccccccccc || exit 1
# echo -n "0: ffff ffff ffff ffff" | xxd -r - "${backend_dev}" || exit 1
# echo -n "0: ffff ffff ffff ffff" | xxd -r | dd of=${backend_dev} conv=fdatasync oflag=direct || exit 1

# Arguments
all_args=("$@")
# all_args[1]="${covert_data_bits}"
export append_args="${all_args[*]}"
echo "append_args: ${append_args}"

# Prepare host CPU into performance mode
if (( set_cpu_perf_mode == 1 )); then
	echo "Set host CPU into performance mode"
	for line in $(find /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor); do
		echo "performance" >"$line"
	done
fi

# Run QEMU
echo "Starting the QEMU"
if ((use_tmux == 1)); then
	tmux_session_name="cross-vm-covert-${label}"
	tmux set-option remain-on-exit on

	tmux start-server
	tmux new-session -d -s "${tmux_session_name}"
	tmux send-keys -t "${tmux_session_name}" "export QEMU=\"${QEMU}\"" Enter
	tmux send-keys -t "${tmux_session_name}" "${qemu_command}"
	tmux send-keys -t "${tmux_session_name}" Enter
	if [ "${debug}" == "on" ]; then
		tmux splitw -h -p 50
		tmux send-keys -t "${tmux_session_name}" "sleep 2; telnet 127.0.0.1 1234"
		tmux send-keys -t "${tmux_session_name}" Enter
	fi
	tmux -2 attach-session -d
else
	echo QEMU=${QEMU}
	echo qemu_command: ${qemu_command}
	${qemu_command}
fi
