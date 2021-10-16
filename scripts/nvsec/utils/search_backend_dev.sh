#!/bin/bash

search_backend_dev() {
	# $1 sender|receiver
	if [ $# -ne 1 ]; then
		echo "Wrong arguments, expected: sender|receiver"
		exit 1
	fi

	if [ $1 != 'sender' ] && [ $1 != 'receiver' ]; then
		echo "Wrong arguments, expected: sender|receiver"
		exit 1
	fi

	label="$1"
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

	echo "${backend_dev}"
}
