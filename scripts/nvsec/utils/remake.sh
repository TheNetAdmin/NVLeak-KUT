#!/bin/bash

rename_nvsec() {
	make clean || true
	make distclean || true

	./configure
	make -j 10 x86/nvsec_covert.flat
}
