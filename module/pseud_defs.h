/* SPDX-License-Identifier: MIT */
/*
 * Linux kernel module that implements pseudo-device driver.
 *
 * Copyright (c) 2022 Dmitry Dolenko
 */

#ifndef PSEUD_H
#define PSEUD_H

#ifndef __KERNEL__
#include <unistd.h>
#endif

#define DRIVER_NAME "pseud"
#define PSEUD_MAJOR 0 /* 0 for dynamic registration */
#define PSEUD_BASEMINOR 0

#define MAX_PSEUD 32
#define PSEUD_MINORS MAX_PSEUD

#ifdef __KERNEL__
#define DEVMEM_LEN PAGE_SIZE
#else
#define DEVMEM_LEN sysconf(_SC_PAGESIZE)
#endif

#define ADDRESS_ATTR_NAME "address"
#define VALUE_ATTR_NAME "value"

#endif /* PSEUD_H */
