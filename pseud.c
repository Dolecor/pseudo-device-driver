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
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "pseud.h"

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Dmitry Dolenko <dolenko.dv@yandex.ru>");
MODULE_DESCRIPTION("Pseudo-device driver");

static u32 pseud_major; /* actual major number*/
static struct class *pseud_class;

/* List of all successfully probed devices */
static LIST_HEAD(pseud_list);
/* Bitmap for managing the available device ids */
static DECLARE_BITMAP(pseud_list_ids, PSEUD_MINORS);
static struct spinlock pseud_list_lock;

int pseud_open(struct inode *, struct file *);
ssize_t pseud_read(struct file *, char __user *, size_t, loff_t *);
ssize_t pseud_write(struct file *, const char __user *uf, size_t, loff_t *);
int pseud_release(struct inode *, struct file *);
loff_t pseud_llseek(struct file *, loff_t, int);
int pseud_mmap(struct file *, struct vm_area_struct *);

static struct file_operations pseud_ops = {
    .owner = THIS_MODULE,
    .open = &pseud_open,
    .read = &pseud_read,
    .write = &pseud_write,
    .release = &pseud_release,
    .llseek = &pseud_llseek,
    .mmap = &pseud_mmap,
};

static int init_pseud_data(struct pseud_data *pseud_data,
                           const struct platform_device *pdev)
{
    struct device *dev;
    int err;

    pseud_data->devmem = kzalloc(DEVMEM_LEN, GFP_KERNEL);
    if (!pseud_data->devmem) {
        err = -ENOMEM;
        goto fail_alloc_devmem;
    }

    cdev_init(&pseud_data->cdev, &pseud_ops);
    pseud_data->cdev.owner = THIS_MODULE;

    err = cdev_add(&pseud_data->cdev, MKDEV(pseud_major, PSEUD_BASEMINOR),
                   PSEUD_MINORS);
    if (err) {
        dev_err(&pdev->dev, "cdev_add failed\n");
        cdev_del(&pseud_data->cdev);
        goto fail_cdev_add;
    }

    dev = device_create(pseud_class, NULL, MKDEV(pseud_major, pdev->id), NULL,
                        "%s_%d", pdev->name, pdev->id);
    if (IS_ERR(dev)) {
        dev_err(&pdev->dev, "device_create failed\n");
        err = PTR_ERR(dev);
        goto fail_device_create;
    }

    return 0;

fail_device_create:
    cdev_del(&pseud_data->cdev);
fail_cdev_add:
    kfree(pseud_data->devmem);
fail_alloc_devmem:
    return err;
}

static void free_pseud_data(struct pseud_data *pseud_data,
                            const struct platform_device *pdev)
{
    device_destroy(pseud_class, MKDEV(pseud_major, pdev->id));
    cdev_del(&pseud_data->cdev);
    kfree(pseud_data->devmem);
}

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

static int pseud_driver_probe(struct platform_device *pdev)
{
    struct pseud_data *pseud_data;
    int err;

    if (test_bit(pdev->id, pseud_list_ids)) {
        dev_err(
            &pdev->dev,
            "device with specified id (minor number) is already registered\n");
        err = -EINVAL;
        goto fail;
    }

    pseud_data = kzalloc(sizeof(struct pseud_data), GFP_KERNEL);
    if (!pseud_data) {
        err = -ENOMEM;
        goto fail;
    }

    err = init_pseud_data(pseud_data, pdev);
    if (err) {
        dev_err(&pdev->dev, "init_pseud_data failed\n");
        goto fail_init_pseud_data;
    }

    pseud_data->id = pdev->id;

    platform_set_drvdata(pdev, pseud_data);

    spin_lock(&pseud_list_lock);
    set_bit(pseud_data->id, pseud_list_ids);
    list_add(&pseud_data->list, &pseud_list);
    spin_unlock(&pseud_list_lock);

    dev_info(&pdev->dev, "created\n");

    return 0;

fail_init_pseud_data:
    kfree(pseud_data);
fail:
    return err;
}

static int pseud_driver_remove(struct platform_device *pdev)
{
    struct pseud_data *pseud_data;

    pseud_data = platform_get_drvdata(pdev);

    spin_lock(&pseud_list_lock);
    list_del(&pseud_data->list);
    clear_bit(pseud_data->id, pseud_list_ids);
    spin_unlock(&pseud_list_lock);

    free_pseud_data(pseud_data, pdev);

    kfree(pseud_data);

    dev_info(&pdev->dev, "removed\n");

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
    dev_info(dev, "released\n");
}

static struct platform_device pseud_devs_reg[] = {
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

    for (nr_reg = 0; nr_reg < ARRAY_SIZE(pseud_devs_reg); ++nr_reg) {
        ret = platform_device_register(&pseud_devs_reg[nr_reg]);
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

    pr_info("%s driver registered with major number %u\n", THIS_MODULE->name,
            pseud_major);

    return 0;

fail_pdrv_reg:
    platform_driver_unregister(&pseud_driver);

fail_pdev_reg:
    for (int i = 0; i < nr_reg; ++i) {
        platform_device_unregister(&pseud_devs_reg[i]);
    }

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

    for (int i = 0; i < ARRAY_SIZE(pseud_devs_reg); ++i) {
        platform_device_unregister(&pseud_devs_reg[i]);
    }

    class_destroy(pseud_class);

    dev = MKDEV(pseud_major, PSEUD_BASEMINOR);
    unregister_chrdev_region(dev, PSEUD_MINORS);
}

module_init(pseud_init);
module_exit(pseud_exit);
