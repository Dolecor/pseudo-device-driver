# SPDX-License-Identifier: MIT

all: module test

module:
	$(MAKE) -C ./module

module-clean:
	$(MAKE) -C ./module clean

test:
	$(MAKE) -C ./test

test-clean:
	$(MAKE) -C ./test clean

clean: module-clean test-clean

.PHONY: all clean module module-clean test test-clean
