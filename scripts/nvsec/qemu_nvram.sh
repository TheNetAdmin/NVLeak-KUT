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

# Check if PMEM is mounted
echo "Check if PMEM is mounted"
dax_mnt="/mnt/dax"
dax_mounted="$(mount -v | grep -c "${dax_mnt}")"
if [ "${dax_mounted}" -ne 1 ]; then
	echo "Please check if pmem is mounted at ${dax_mnt}"
	exit 1
fi

# RAM configs
ram_size="1G"
ram_slots="2"     # >= number of ram + number of nvram
ram_max_size="2G" # >= $ram_size + $nvram_size

# QEMU configs
nvram_size="1G"
nvram_backend_file="/${dax_mnt}/data_${label}"

# QEMU args
qemu_comm_args=""
qemu_comm_args+=" -machine pc,nvdimm,accel=kvm"
qemu_comm_args+=" -m ${ram_size},slots=${ram_slots},maxmem=${ram_max_size}"
qemu_comm_args+=" -object memory-backend-file,id=mem1,share=on,mem-path="${nvram_backend_file}",size=${nvram_size}"
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


echo "Creating NVDIMM backend file"
fallocate -l "${nvram_size}" "${nvram_backend_file}"

echo "Dump magic data to the backend file"
echo -n "0: ffff ffff ffff ffff" | xxd -r - "${nvram_backend_file}"

# Run QEMU in TMUX
echo "Starting the QEMU"
tmux_session_name="cross-vm-covert-${label}"
tmux set-option remain-on-exit on

tmux start-server
tmux new-session -d -s "${tmux_session_name}"
tmux send-keys -t "${tmux_session_name}" "${qemu_command}"
tmux send-keys -t "${tmux_session_name}" Enter
if [ "${debug}" == "on" ]; then
	tmux splitw -h -p 50
	tmux send-keys -t "${tmux_session_name}" "sleep 2; telnet 127.0.0.1 1234"
	tmux send-keys -t "${tmux_session_name}" Enter
fi
tmux -2 attach-session -d
