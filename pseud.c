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
#include <linux/fs.h>

#include "pseud.h"

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Dmitry Dolenko <dolenko.dv@yandex.ru>");
MODULE_DESCRIPTION("Pseudo-device driver");

static u16 pseud_major; /* actual major number*/
static struct class *pseud_class;

int pseud_open(struct inode *inode, struct file *filp)
{
    pr_info("%s, major: %d, minor: %d", __func__, imajor(inode), iminor(inode));
    return 0;
}

ssize_t pseud_read(struct file *filp, char __user *buf, size_t count,
                   loff_t *off)
{
    pr_info("%s is not implemented", __func__);
    return 0;
}

ssize_t pseud_write(struct file *filp, const char __user *buf, size_t count,
                    loff_t *off)
{
    pr_info("%s is not implemented", __func__);
    return count;
}

int pseud_release(struct inode *inode, struct file *filp)
{
    pr_info("%s is not implemented", __func__);
    return 0;
}

loff_t pseud_llseek(struct file *filp, loff_t off, int whence)
{
    pr_info("%s is not implemented", __func__);
    return 0;
}

int pseud_mmap(struct file *filp, struct vm_area_struct *vma)
{
    pr_info("%s is not implemented", __func__);
    return 0;
}

static struct file_operations pseud_ops = {
    .owner = THIS_MODULE,
    .open = &pseud_open,
    .read = &pseud_read,
    .write = &pseud_write,
    .release = &pseud_release,
    .llseek = &pseud_llseek,
    .mmap = &pseud_mmap,
};

static int pseud_driver_probe(struct platform_device *pdev)
{
    struct cdev *cdev;
    int err;

    pr_info("%s: probe\n", THIS_MODULE->name);

    // TODO: change to:
    // kzalloc pseud_device_data
    // use cdev_init(pseud_dev_data->cdev) instead of cdev_alloc

    cdev = cdev_alloc();

    cdev->owner = THIS_MODULE;
    cdev->ops = &pseud_ops;
    cdev->dev = MKDEV(pseud_major, PSEUD_BASEMINOR);

    err = cdev_add(cdev, cdev->dev, PSEUD_MINORS);
    if (err) {
        pr_info("can not add cdev");
        cdev_del(cdev);
        return err;
    }

    if (device_create(pseud_class, NULL, MKDEV(pseud_major, pdev->id), NULL,
                      "pseud_%d", pdev->id)) {
        pr_info("cdev pseud_%d created", pdev->id);
    } else {
        pr_err("failed to create cdev\n");
    }

    platform_set_drvdata(pdev, cdev);

    return 0;
}

static int pseud_driver_remove(struct platform_device *pdev)
{
    struct cdev *cdev;

    pr_info("%s: remove\n", THIS_MODULE->name);

    cdev = platform_get_drvdata(pdev);

    device_destroy(pseud_class, MKDEV(pseud_major, MINOR(cdev->dev)));

    cdev_del(cdev);

    return 0;
}

static struct platform_driver pseud_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .owner = THIS_MODULE,
    },
    .probe = pseud_driver_probe,
    .remove = pseud_driver_remove,
};

void pseud_device_release(struct device *dev)
{
    pr_info("Pseud device release\n");
}

static struct platform_device pseud_devices[] = {
    {
        .name = DRIVER_NAME,
        .id = 0,
        .dev = {
            .release = pseud_device_release,
        }
    },
    {
        .name = DRIVER_NAME,
        .id = 1,
        .dev = {
            .release = pseud_device_release,
        }
    },
    {
        .name = DRIVER_NAME,
        .id = 2,
        .dev = {
            .release = pseud_device_release,
        }
    },
};

static int __init pseud_init(void)
{
    int ret;
    dev_t pseud_dev;
    int nr_reg; /* number of registered devices */

    pr_info("%s: Init\n", THIS_MODULE->name);

    if (PSEUD_MAJOR == 0) {
        ret = alloc_chrdev_region(&pseud_dev, PSEUD_BASEMINOR, PSEUD_MINORS,
                                  DRIVER_NAME);
        if (ret < 0) {
            pr_err("Can not allocate chrdev region\n");
            goto fail_alloc_cregion;
        }

        pseud_major = MAJOR(pseud_dev);
    } else {
        pr_err("PSEUD_MAJOR is not 0, but static major number assignment with "
               "register_chrdev_region is not implemented\n");
        ret = -1;
        goto fail_alloc_cregion;
    }

    pseud_class = class_create(THIS_MODULE, DRIVER_NAME);
    if (IS_ERR(pseud_class)) {
        ret = PTR_ERR(pseud_class);
        goto fail_class_create;
    }

    for (nr_reg = 0; nr_reg < ARRAY_SIZE(pseud_devices); ++nr_reg) {
        ret = platform_device_register(&pseud_devices[nr_reg]);
        if (ret < 0) {
            pr_err("Can not register platform device (retcode: %d)\n", ret);
            goto fail_pdev_reg;
        }
    }

    ret = platform_driver_register(&pseud_driver);
    if (ret) {
        pr_err("Can not register platform driver (retcode: %d)\n", ret);
        goto fail_pdrv_reg;
    }

    pr_info("Pseud registered with major number %u\n", pseud_major);

    return 0;

fail_pdrv_reg:
    platform_driver_unregister(&pseud_driver);

fail_pdev_reg:
    for (int i = 0; i < nr_reg; ++i) {
        platform_device_unregister(&pseud_devices[i]);
    }

    class_unregister(pseud_class);
    class_destroy(pseud_class);

fail_class_create:
    unregister_chrdev_region(pseud_dev, PSEUD_MINORS);

fail_alloc_cregion:
    return ret;
}

static void __exit pseud_exit(void)
{
    dev_t dev;

    pr_info("%s: Exit\n", THIS_MODULE->name);

    platform_driver_unregister(&pseud_driver);

    for (int i = 0; i < ARRAY_SIZE(pseud_devices); ++i) {
        platform_device_unregister(&pseud_devices[i]);
    }
    
    class_destroy(pseud_class);

    dev = MKDEV(pseud_major, PSEUD_BASEMINOR);
    unregister_chrdev_region(dev, PSEUD_MINORS);
}

module_init(pseud_init);
module_exit(pseud_exit);
