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
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/sysfs.h>

#include "pseud_defs.h"

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Dmitry Dolenko <dolenko.dv@yandex.ru>");
MODULE_DESCRIPTION("Pseudo-device driver");

struct pseud_data {
    u8 *devmem;
    struct mutex devmem_mtx;

    struct cdev cdev;

    /* sysfs */
    struct device *dev;
    loff_t address;
    struct device_attribute address_attr;
    struct device_attribute value_attr;
};

static u32 pseud_major; /* actual major number */
static struct class *pseud_class;

static int pseud_open(struct inode *, struct file *);
static ssize_t pseud_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t pseud_write(struct file *, const char __user *uf, size_t,
                           loff_t *);
static int pseud_release(struct inode *, struct file *);
static loff_t pseud_llseek(struct file *, loff_t, int);
static int pseud_mmap(struct file *, struct vm_area_struct *);

static struct file_operations pseud_ops = {
    .owner = THIS_MODULE,
    .open = &pseud_open,
    .read = &pseud_read,
    .write = &pseud_write,
    .release = &pseud_release,
    .llseek = &pseud_llseek,
    .mmap = &pseud_mmap,
};

static ssize_t address_show(struct device *, struct device_attribute *, char *);
static ssize_t address_store(struct device *, struct device_attribute *,
                             const char *, size_t);
static ssize_t value_show(struct device *, struct device_attribute *, char *);
static ssize_t value_store(struct device *, struct device_attribute *,
                           const char *, size_t);

static int init_pseud_sysfs(struct pseud_data *pseud_data,
                            struct platform_device *pdev)
{
    int err;
    dev_t devt = MKDEV(pseud_major, pdev->id);

    pseud_data->dev = device_create(pseud_class, &pdev->dev, devt, pseud_data,
                                    "%s_%d", pdev->name, pdev->id);

    if (IS_ERR(pseud_data->dev)) {
        dev_err(&pdev->dev, "device_create failed\n");
        err = PTR_ERR(pseud_data->dev);
        goto fail;
    }

    sysfs_attr_init(pseud_data->address_attr);
    pseud_data->address_attr.attr.name = ADDRESS_ATTR_NAME;
    pseud_data->address_attr.attr.mode = S_IRUGO | S_IWUSR; /* 0644 */
    pseud_data->address_attr.show = address_show;
    pseud_data->address_attr.store = address_store;
    err = device_create_file(pseud_data->dev, &pseud_data->address_attr);
    if (err) {
        goto fail_attr;
    }

    sysfs_attr_init(pseud_data->value_attr);
    pseud_data->value_attr.attr.name = VALUE_ATTR_NAME;
    pseud_data->value_attr.attr.mode = S_IRUGO | S_IWUSR; /* 0644 */
    pseud_data->value_attr.show = value_show;
    pseud_data->value_attr.store = value_store;
    err = device_create_file(pseud_data->dev, &pseud_data->value_attr);
    if (err) {
        device_remove_file(pseud_data->dev, &pseud_data->address_attr);
        goto fail_attr;
    }

    return 0;

fail_attr:
    device_destroy(pseud_class, devt);
fail:
    return err;
}

static void free_pseud_sysfs(struct pseud_data *pseud_data,
                             const struct platform_device *pdev)
{
    device_remove_file(pseud_data->dev, &pseud_data->address_attr);
    device_remove_file(pseud_data->dev, &pseud_data->value_attr);
    device_destroy(pseud_class, MKDEV(pseud_major, pdev->id));
}

static ssize_t address_show(struct device *dev, struct device_attribute *attr,
                            char *buf)
{
    struct pseud_data *data = dev_get_drvdata(dev);
    return sprintf(buf, "%lld\n", data->address);
}

static ssize_t address_store(struct device *dev, struct device_attribute *attr,
                             const char *buf, size_t count)
{
    struct pseud_data *data = dev_get_drvdata(dev);
    loff_t address;

    if ((sscanf(buf, "%lld", &address) != 1) || (address < 0)
        || (address > DEVMEM_LEN - 1)) {
        dev_err(dev, "invalid address\n");
        return -EINVAL;
    }

    data->address = address;

    return count;
}

static ssize_t value_show(struct device *dev, struct device_attribute *attr,
                          char *buf)
{
    struct pseud_data *data = dev_get_drvdata(dev);
    return sprintf(buf, "%u\n", data->devmem[data->address]);
}

static ssize_t value_store(struct device *dev, struct device_attribute *attr,
                           const char *buf, size_t count)
{
    struct pseud_data *data = dev_get_drvdata(dev);
    s32 value;

    if ((sscanf(buf, "%d", &value) != 1) || (value < 0) || (value > 255)) {
        dev_err(dev, "invalid value\n");
        return -EINVAL;
    }

    data->devmem[data->address] = (u8)value;

    return count;
}

static int init_pseud_data(struct pseud_data *pseud_data,
                           struct platform_device *pdev)
{
    int err;

    pseud_data->devmem = kzalloc(DEVMEM_LEN, GFP_KERNEL);
    if (!pseud_data->devmem) {
        err = -ENOMEM;
        goto fail_alloc_devmem;
    }

    mutex_init(&pseud_data->devmem_mtx);

    cdev_init(&pseud_data->cdev, &pseud_ops);
    pseud_data->cdev.owner = THIS_MODULE;

    err = cdev_add(&pseud_data->cdev, MKDEV(pseud_major, pdev->id), 1);
    if (err) {
        dev_err(&pdev->dev, "cdev_add failed\n");
        goto fail_cdev_add;
    }

    err = init_pseud_sysfs(pseud_data, pdev);
    if (err) {
        dev_err(&pdev->dev, "init_pseud_sysfs failed\n");
        goto fail_sysfs;
    }

    return 0;

fail_sysfs:
    cdev_del(&pseud_data->cdev);
fail_cdev_add:
    kfree(pseud_data->devmem);
fail_alloc_devmem:
    return err;
}

static void free_pseud_data(struct pseud_data *pseud_data,
                            const struct platform_device *pdev)
{
    free_pseud_sysfs(pseud_data, pdev);
    cdev_del(&pseud_data->cdev);
    kfree(pseud_data->devmem);
}

static int pseud_open(struct inode *inode, struct file *filp)
{
    struct pseud_data *data;

    data = container_of(inode->i_cdev, struct pseud_data, cdev);
    filp->private_data = data;

    pr_debug("%s: %s (major %d, minor %d)\n", __func__,
             filp->f_path.dentry->d_iname, imajor(inode), iminor(inode));

    return 0;
}

static ssize_t pseud_read(struct file *filp, char __user *buf, size_t count,
                          loff_t *off)
{
    struct pseud_data *data = filp->private_data;
    ssize_t ret = 0;

    if ((filp->f_flags & O_NONBLOCK)) {
        if (!mutex_trylock(&data->devmem_mtx)) {
            return -EAGAIN;
        }
    } else {
        if (mutex_lock_interruptible(&data->devmem_mtx)) {
            return -ERESTARTSYS;
        }
    }

    if (*off >= DEVMEM_LEN) {
        ret = 0;
        goto fail;
    }
    if (*off + count > DEVMEM_LEN) {
        count = DEVMEM_LEN - *off;
    }

    if (copy_to_user(buf, data->devmem + *off, count)) {
        ret = -EFAULT;
        goto fail;
    }

    *off += count;
    ret = count;

fail:
    mutex_unlock(&data->devmem_mtx);
    pr_debug("%s: %s (read %zd bytes)\n", __func__,
             filp->f_path.dentry->d_iname, ret);
    return ret;
}

static ssize_t pseud_write(struct file *filp, const char __user *buf,
                           size_t count, loff_t *off)
{
    struct pseud_data *data = filp->private_data;
    ssize_t ret = 0;

    if ((filp->f_flags & O_NONBLOCK)) {
        if (!mutex_trylock(&data->devmem_mtx)) {
            return -EAGAIN;
        }
    } else {
        if (mutex_lock_interruptible(&data->devmem_mtx)) {
            return -ERESTARTSYS;
        }
    }

    if (*off + count > DEVMEM_LEN) {
        count = DEVMEM_LEN - *off;
    }

    if (copy_from_user(data->devmem + *off, buf, count)) {
        ret = -EFAULT;
        goto out;
    }

    *off += count;
    ret = count;

out:
    mutex_unlock(&data->devmem_mtx);
    pr_debug("%s: %s (written %zd bytes)\n", __func__,
             filp->f_path.dentry->d_iname, ret);
    return ret;
}

static int pseud_release(struct inode *inode, struct file *filp)
{
    filp->private_data = NULL;
    pr_debug("%s: %s\n", __func__, filp->f_path.dentry->d_iname);
    return 0;
}

static loff_t pseud_llseek(struct file *filp, loff_t off, int whence)
{
    struct pseud_data *data = filp->private_data;
    loff_t new_pos;

    mutex_lock(&data->devmem_mtx);

    switch (whence) {
    case SEEK_SET:
        new_pos = off;
        break;
    case SEEK_CUR:
        new_pos = filp->f_pos + off;
        break;
    case SEEK_END:
        new_pos = DEVMEM_LEN + off;
        break;
    default:
        return -EINVAL;
    }

    if (new_pos > DEVMEM_LEN) {
        new_pos = DEVMEM_LEN;
    }
    if (new_pos < 0) {
        new_pos = 0;
    }

    filp->f_pos = new_pos;

    mutex_unlock(&data->devmem_mtx);
    pr_debug("%s: %s (new pos: %lld)\n", __func__, filp->f_path.dentry->d_iname,
             new_pos);
    return new_pos;
}

static int pseud_mmap(struct file *filp, struct vm_area_struct *vma)
{
    int err;
    struct pseud_data *data = filp->private_data;
    struct page *page;
    size_t size = vma->vm_end - vma->vm_start;
    unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;

    if (!virt_addr_valid(data->devmem)) {
        pr_err("virt_addr_valid failed\n");
        return -EIO;
    }
    page = virt_to_page(data->devmem + offset);

    err = remap_pfn_range(vma, vma->vm_start, page_to_pfn(page), size,
                          vma->vm_page_prot);

    if (err) {
        pr_err("remap_pfn_range failed for %s\n", filp->f_path.dentry->d_iname);
        return err;
    }

    pr_debug("%s: %s\n", __func__, filp->f_path.dentry->d_iname);

    return 0;
}

static int pseud_driver_probe(struct platform_device *pdev)
{
    struct pseud_data *pseud_data;
    int err;

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

    platform_set_drvdata(pdev, pseud_data);

    dev_info(&pdev->dev, "created\n");

    return 0;

fail_init_pseud_data:
    kfree(pseud_data);
fail:
    return err;
}

static int pseud_driver_remove(struct platform_device *pdev)
{
    struct pseud_data *pseud_data = platform_get_drvdata(pdev);

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
    int err;
    dev_t pseud_dev;
    int nr_reg; /* number of registered devices */

    pr_info("%s: Init\n", THIS_MODULE->name);

    if (PSEUD_MAJOR == 0) {
        err = alloc_chrdev_region(&pseud_dev, PSEUD_BASEMINOR, PSEUD_MINORS,
                                  DRIVER_NAME);
        if (err) {
            pr_err("Can not allocate chrdev region\n");
            goto fail_alloc_cregion;
        }

        pseud_major = MAJOR(pseud_dev);
    } else {
        pr_err("PSEUD_MAJOR is not 0, but static major number assignment with "
               "register_chrdev_region is not implemented\n");
        err = -1;
        goto fail_alloc_cregion;
    }

    pseud_class = class_create(THIS_MODULE, DRIVER_NAME);
    if (IS_ERR(pseud_class)) {
        err = PTR_ERR(pseud_class);
        goto fail_class_create;
    }

    for (nr_reg = 0; nr_reg < ARRAY_SIZE(pseud_devs_reg); ++nr_reg) {
        err = platform_device_register(&pseud_devs_reg[nr_reg]);
        if (err) {
            pr_err("Can not register platform device (retcode: %d)\n", err);
            goto fail_pdev_reg;
        }
    }

    err = platform_driver_register(&pseud_driver);
    if (err) {
        pr_err("Can not register platform driver (retcode: %d)\n", err);
        goto fail_pdrv_reg;
    }

    pr_info("%s registered with major number %u\n", THIS_MODULE->name,
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
    return err;
}

static void __exit pseud_exit(void)
{
    dev_t dev = MKDEV(pseud_major, PSEUD_BASEMINOR);

    pr_info("%s: Exit\n", THIS_MODULE->name);

    platform_driver_unregister(&pseud_driver);

    for (int i = 0; i < ARRAY_SIZE(pseud_devs_reg); ++i) {
        platform_device_unregister(&pseud_devs_reg[i]);
    }

    class_destroy(pseud_class);

    unregister_chrdev_region(dev, PSEUD_MINORS);
}

module_init(pseud_init);
module_exit(pseud_exit);
