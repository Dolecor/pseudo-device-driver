# SPDX-License-Identifier: MIT

obj-m += pseud.o
ccflags-y += -Wall -std=gnu11 -DDEBUG

ifdef HEADERS
	LINUX_HEADERS := $(HEADERS)
else
	VERSION := $(shell uname -r)
	LINUX_HEADERS := /lib/modules/$(VERSION)/build
endif

PWD := $(shell pwd)

all:
	make -C $(LINUX_HEADERS) M=$(PWD) modules

clean:
	make -C $(LINUX_HEADERS) M=$(PWD) clean
