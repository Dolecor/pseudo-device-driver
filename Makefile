# SPDX-License-Identifier: MIT

obj-m += pseud.o
ccflags-y += -std=gnu11 -DDEBUG


VERSION := $(shell uname -r)
LINUX_HEADERS := /lib/modules/$(VERSION)/build

PWD := $(shell pwd)

all:
	make -C $(LINUX_HEADERS) M=$(PWD) modules

clean:
	make -C $(LINUX_HEADERS) M=$(PWD) clean
