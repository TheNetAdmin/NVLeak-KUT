#!/bin/bash

set -e

if [ $# -ne 1 ]; then
	echo "$0 pmem_dev"
	exit 1
fi

pmem_dev="${1}"

if ! [[ "${pmem_dev}" == "/dev/pmem"* ]]; then
	echo "You should pass a pmem device, not ${pmem_dev}"
	exit 2
fi

mkfs.ext4 "${pmem_dev}"
mkdir -p /mnt/dax
mount -o dax "${pmem_dev}" /mnt/dax
mount -v | grep "${pmem_dev}"
