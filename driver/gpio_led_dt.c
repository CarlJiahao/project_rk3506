/**
 * @file    gpio_led_dt.c
 * @brief   GPIO LED 驱动 — 通过设备树获取 GPIO（标准做法）
 * @author  Carl
 * @date    2026-06-21
 *
 * 和 gpio_led.c 的区别：
 *   gpio_led.c:  写死 #define LED_GPIO 35（高手不用这种方式）
 *   gpio_led_dt.c: 从设备树 gpios 属性读取 GPIO（标准驱动写法）
 *
 * 学习要点：
 *   1. platform_driver — 平台驱动框架
 *   2. of_match_table  — compatible 匹配设备树节点
 *   3. gpiod_get()    — 从设备树获取 GPIO 描述符
 *   4. gpiod_set_value() / gpiod_direction_output()
 *   5. probe / remove  — 设备插入/移除时的回调
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/gpio/consumer.h>    /* gpiod_* API — 设备树 GPIO 接口 */
#include <linux/platform_device.h> /* platform_driver, probe, remove */
#include <linux/of.h>              /* of_match_table */
#include <linux/slab.h>

#define DEVICE_NAME     "gpio_led_dt"
#define CLASS_NAME      "gpio_led_dt_class"

static int major_number;
static struct class *gpio_led_class = NULL;
static struct device *gpio_led_device = NULL;
static struct cdev gpio_led_cdev;
static struct gpio_desc *led_gpio = NULL;  /* 设备树给的 GPIO 描述符 */

/* ================================================================
 * file_operations（和之前一样，只是 gpio_* 换成 gpiod_*）
 * ================================================================ */

static int gpio_led_open(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "[gpio_led_dt] open\n");
    return 0;
}

static int gpio_led_release(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "[gpio_led_dt] release\n");
    return 0;
}

static ssize_t gpio_led_write(struct file *file, const char __user *buf,
                               size_t len, loff_t *offset)
{
    char cmd;

    if (copy_from_user(&cmd, buf, 1))
        return -EFAULT;

    if (cmd == '\n')
        return len;

    if (cmd == '1') {
        gpiod_set_value(led_gpio, 1);     /* gpiod_ 版本 */
        printk(KERN_INFO "[gpio_led_dt] LED ON\n");
    } else if (cmd == '0') {
        gpiod_set_value(led_gpio, 0);
        printk(KERN_INFO "[gpio_led_dt] LED OFF\n");
    } else {
        return -EINVAL;
    }
    return len;
}

static ssize_t gpio_led_read(struct file *file, char __user *buf,
                              size_t len, loff_t *offset)
{
    int val;
    char status;

    if (*offset > 0)
        return 0;

    val = gpiod_get_value(led_gpio);      /* gpiod_ 版本 */
    status = (val == 0) ? '0' : '1';

    if (copy_to_user(buf, &status, 1))
        return -EFAULT;

    *offset = 1;
    return 1;
}

static struct file_operations gpio_led_fops = {
    .owner   = THIS_MODULE,
    .open    = gpio_led_open,
    .release = gpio_led_release,
    .write   = gpio_led_write,
    .read    = gpio_led_read,
};

/* ================================================================
 * platform_driver: probe / remove
 * ================================================================
 * probe  — 内核发现设备树里 compatible 匹配的节点时调用
 * remove — 模块卸载或设备移除时调用
 */

static int gpio_led_probe(struct platform_device *pdev)
{
    int ret;
    dev_t dev_num;

    printk(KERN_INFO "[gpio_led_dt] ========== probe 开始 ==========\n");

    /* --- 从设备树获取 GPIO --- */
    led_gpio = gpiod_get(&pdev->dev, NULL, GPIOD_OUT_LOW);
    if (IS_ERR(led_gpio)) {
        ret = PTR_ERR(led_gpio);
        printk(KERN_ERR "[gpio_led_dt] gpiod_get 失败: %d\n", ret);
        return ret;
    }
    printk(KERN_INFO "[gpio_led_dt] GPIO 已从设备树获取，初始=LOW\n");

    /* --- 注册字符设备 --- */
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        printk(KERN_ERR "[gpio_led_dt] alloc_chrdev_region 失败\n");
        goto fail_region;
    }
    major_number = MAJOR(dev_num);

    cdev_init(&gpio_led_cdev, &gpio_led_fops);
    gpio_led_cdev.owner = THIS_MODULE;
    ret = cdev_add(&gpio_led_cdev, dev_num, 1);
    if (ret < 0)
        goto fail_cdev;

    gpio_led_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(gpio_led_class)) {
        ret = PTR_ERR(gpio_led_class);
        goto fail_class;
    }

    gpio_led_device = device_create(gpio_led_class, NULL, dev_num,
                                     NULL, DEVICE_NAME);
    if (IS_ERR(gpio_led_device)) {
        ret = PTR_ERR(gpio_led_device);
        goto fail_device;
    }

    printk(KERN_INFO "[gpio_led_dt] ========== 就绪 ==========\n");
    printk(KERN_INFO "[gpio_led_dt] 设备: /dev/%s  主设备号: %d\n",
           DEVICE_NAME, major_number);
    return 0;

    /* 错误回滚 */
fail_device:
    class_destroy(gpio_led_class);
fail_class:
    cdev_del(&gpio_led_cdev);
fail_cdev:
    unregister_chrdev_region(dev_num, 1);
fail_region:
    gpiod_put(led_gpio);
    return ret;
}

static int gpio_led_remove(struct platform_device *pdev)
{
    dev_t dev_num = MKDEV(major_number, 0);

    printk(KERN_INFO "[gpio_led_dt] ========== remove ==========\n");

    device_destroy(gpio_led_class, dev_num);
    class_destroy(gpio_led_class);
    cdev_del(&gpio_led_cdev);
    unregister_chrdev_region(dev_num, 1);
    gpiod_put(led_gpio);    /* 释放设备树 GPIO */

    printk(KERN_INFO "[gpio_led_dt] GPIO 已释放\n");
    return 0;
}

/* ================================================================
 * 设备树匹配表 — 这就是"接头"！
 * ================================================================ */
static const struct of_device_id gpio_led_dt_ids[] = {
    { .compatible = "carl,gpio-led" },     /* ← 设备树里写这个字符串 */
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, gpio_led_dt_ids);

static struct platform_driver gpio_led_driver = {
    .driver = {
        .name           = DEVICE_NAME,
        .owner          = THIS_MODULE,
        .of_match_table = gpio_led_dt_ids,  /* 匹配设备树 compatible */
    },
    .probe  = gpio_led_probe,    /* 匹配成功 → 调 probe */
    .remove = gpio_led_remove,   /* 设备移除 → 调 remove */
};

module_platform_driver(gpio_led_driver);  /* 替代 module_init/module_exit */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Carl");
MODULE_DESCRIPTION("RK3506 GPIO LED Driver — 设备树版本");
MODULE_VERSION("1.0.0");
