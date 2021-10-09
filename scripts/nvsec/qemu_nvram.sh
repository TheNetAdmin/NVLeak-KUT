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
dax_mnt="/mnt/dax"
dax_mounted="$(mount -v | grep -c "${dax_mnt}")"
if [ "${dax_mounted}" -ne 1 ]; then
	echo "Please check if pmem is mounted at ${dax_mnt}"
	exit 1
fi

# qemu="qemu-system-x86_64"
ram_size="1G"
ram_slots="2" # >= number of ram + number of nvram
ram_max_size="2G" # >= $ram_size + $nvram_size

nvram_size="1G"
nvram_backend_file="/${dax_mnt}/data_${label}"

read -r -d '' qemu_command << EOM
x86/run \
	"${kernel_path}" \
	-machine pc,nvdimm,accel=kvm \
	-m ${ram_size},slots=${ram_slots},maxmem=${ram_max_size} \
	-object memory-backend-file,id=mem1,share=on,mem-path="${nvram_backend_file}",size=${nvram_size} \
	-device nvdimm,id=nvdimm1,memdev=mem1 \
;
EOM

bash_run="env -i bash --norc --noprofile"

# Run QEMU in TMUX
echo "Starting the QEMU"
tmux_session_name="cross-vm-covert-${label}"
tmux set-option remain-on-exit on

tmux start-server
tmux new-session -d -s "${tmux_session_name}" "${bash_run}"
tmux send-keys -t "${tmux_session_name}" "${qemu_command}"
tmux send-keys -t "${tmux_session_name}" Enter
tmux -2 attach-session -d
