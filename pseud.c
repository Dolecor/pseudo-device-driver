// SPDX-License-Identifier: MIT
/*
 * Linux kernel module that implements pseudo-device driver.
 *
 * Copyright (c) 2022 Dmitry Dolenko
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include "pseud.h"

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Dmitry Dolenko <dolenko.dv@yandex.ru>");
// MODULE_DESCRIPTION();
// MODULE_DEVICE_TABLE();

static int __init pseud_init(void)
{
    pr_info("%s: test init\n", THIS_MODULE->name);
    return 0;
}

static void __exit pseud_exit(void)
{
    pr_info("%s: test exit\n", THIS_MODULE->name);
}

module_init(pseud_init);
module_exit(pseud_exit);
