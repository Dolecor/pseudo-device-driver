/* SPDX-License-Identifier: MIT */
/*
 * Linux kernel module that implements pseudo-device driver.
 *
 * Copyright (c) 2022 Dmitry Dolenko
 */

#ifndef PSEUD_H
#define PSEUD_H

#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/list.h>

#define DRIVER_NAME "pseud"
#define PSEUD_MAJOR 0 /* 0 for dynamic registration */
#define PSEUD_BASEMINOR 0

/* The maximum number of pseudo-devices that the driver can hold.
 * 32 or 64 devices on 32-bit or 64-bit machine respectively.
 */
#define MAX_PSEUD BITS_PER_LONG
#define PSEUD_MINORS MAX_PSEUD

#define DEVMEM_LEN 4096

struct pseud_device_data {
    char *devmem;

    struct list_head list;
    struct cdev cdev;
};

#endif /* PSEUD_H */
