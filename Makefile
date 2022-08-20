# SPDX-License-Identifier: MIT

obj-m += pseud.o


VERSION := $(shell uname -r)
LINUX_HEADERS := /lib/modules/$(VERSION)/build

PWD := $(shell pwd)

all:
	make -C $(LINUX_HEADERS) M=$(PWD) modules

clean:
	make -C $(LINUX_HEADERS) M=$(PWD) clean
