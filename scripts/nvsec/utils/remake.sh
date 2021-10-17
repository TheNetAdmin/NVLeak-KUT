#!/bin/bash

remake_nvsec() {
	make clean || true
	make distclean || true

	./configure
	make -j 10 x86/nvram_covert.flat
}
