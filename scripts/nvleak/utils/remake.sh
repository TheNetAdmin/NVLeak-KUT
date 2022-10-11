#!/bin/bash

remake_nvleak() {
	make clean || true
	make distclean || true

	./configure
	make -j 10 x86/nvram_covert.flat
}
