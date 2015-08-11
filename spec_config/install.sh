#!/bin/bash

install() {
	if [[ $# -ne 1 ]]; then
		echo "usage: $FUNCNAME <path/to/spec/config>"
		return 1
	fi

	local SPEC_CONFIG="$1"

	for l in $(ls -1 *.template);
	do
		local log_name="${l%.*}"
		cp $l ${SPEC_CONFIG}/${log_name}.cfg
	done
}

install $@
