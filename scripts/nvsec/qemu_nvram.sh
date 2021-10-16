#!/bin/bash

curr_path="$(realpath "$(dirname "${0}")")"

usage() {
	echo "Usage: $0 [sender|receiver]"
}

# Arguments
if [ $# -ne 1 ]; then
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
	*)
		usage
		exit 1
	;;
esac

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
backend_name="dax-${label}"
backend_dev=$(ndctl list --namespaces | jq -r ".[] | select(.name == \"${backend_name}\") | .chardev")
if [ $? -ne 0 ]; then
	echo "Failed searching namespace with name: ${backend_name}"
	exit 1
fi
if [ -z "${backend_dev}" ]; then
	echo "Backend not found: ${backend_dev}"
	exit 1
fi
backend_dev="/dev/${backend_dev}"
if [ ! -c "${backend_dev}" ]; then
	echo "Backend device not found: ${backend_dev}"
	exit 1
fi

# RAM configs
ram_size="1G"
ram_slots="2"     # >= number of ram + number of nvram
ram_max_size="2G" # >= $ram_size + $nvram_size

# QEMU path
export QEMU="../qemu/build/x86_64-softmmu/qemu-system-x86_64"

# QEMU configs
nvram_size="1G"
nvram_align_size="1G"

# QEMU args
qemu_comm_args=""
qemu_comm_args+=" -cpu max"
qemu_comm_args+=" -machine pc,nvdimm,accel=kvm"
qemu_comm_args+=" -m ${ram_size},slots=${ram_slots},maxmem=${ram_max_size}"
qemu_comm_args+=" -object memory-backend-file,id=mem1,share=on,mem-path=\"${backend_dev}\",size=${nvram_size},align=${nvram_align_size}"
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

echo "Dump magic data to the backend file"
dumper="$(realpath "${curr_path}/dump")"
gcc "${dumper}.c" -o "${dumper}" || exit 1
${dumper} -f "${backend_dev}" -d 0xcccccccccccccccc || exit 1
# echo -n "0: ffff ffff ffff ffff" | xxd -r - "${backend_dev}" || exit 1
# echo -n "0: ffff ffff ffff ffff" | xxd -r | dd of=${backend_dev} conv=fdatasync oflag=direct || exit 1

# Prepare host CPU into performance mode
echo "Set host CPU into performance mode"
for line in $(find /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor); do
	echo "performance" >"$line"
done

# Run QEMU in TMUX
echo "Starting the QEMU"
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
