#!/bin/bash

search_backend_dev() {
	# $1 sender|receiver
	label="${1}"
	if [ $# -ne 1 ]; then
		echo "Wrong arguments, expected: sender|receiver"
		exit 1
	fi

	if [ "${label}" == 'vanilla' ]; then
		label='sender'
	fi

	if [ "${label}" != 'sender' ] && [ "${label}" != 'receiver' ]; then
		echo "Wrong arguments, expected: sender|receiver"
		exit 1
	fi

	same_devdax="${same_devdax:-}"
	if [ -n "${same_devdax}" ]; then
		# echo "Force using the same devdax with label: ${same_devdax}"
		label="${same_devdax}"
	fi
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
