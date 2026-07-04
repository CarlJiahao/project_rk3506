/**
 * @file    gpio_led.c
 * @brief   GPIO LED 驱动 — 控制 GPIO0_3 输出高/低电平
 * @author  Carl
 * @date    2026-06-21
 *
 * 学习要点：
 *   1. gpio_request() / gpio_free() — 申请/释放 GPIO 引脚
 *   2. gpio_direction_output() — 设置 GPIO 为输出模式
 *   3. gpio_set_value() — 设置输出电平（1 = 高, 0 = 低）
 *   4. 将 GPIO 操作封装在字符设备驱动里，用户程序通过 write 控制
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>         /* gpio_request, gpio_free, gpio_set_value */
#include <linux/slab.h>

#define DEVICE_NAME     "gpio_led"
#define CLASS_NAME      "gpio_led_class"
#define LED_GPIO        35      /* GPIO1_3 — 全局编号 32+3=35 */

static int major_number;
static struct class *gpio_led_class = NULL;
static struct device *gpio_led_device = NULL;
static struct cdev gpio_led_cdev;

/* ================================================================
 * file_operations 实现
 * ================================================================ */

static int gpio_led_open(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "[gpio_led] open: 设备被打开\n");
    return 0;
}

static int gpio_led_release(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "[gpio_led] release: 设备被关闭\n");
    return 0;
}

/**
 * @brief 写 '1' → LED 亮（高电平），写 '0' → LED 灭（低电平）
 */
static ssize_t gpio_led_write(struct file *file, const char __user *buf,
                               size_t len, loff_t *offset)
{
    char cmd;

    if (copy_from_user(&cmd, buf, 1))
        return -EFAULT;

    /* 跳过换行符（echo 默认会带 \n） */
    if (cmd == '\n')
        return len;  /* 只收到换行，什么都不做 */

    if (cmd == '1') {
        gpio_set_value(LED_GPIO, 1);          /* 输出高电平 */
        printk(KERN_INFO "[gpio_led] GPIO%d = HIGH (LED ON)\n", LED_GPIO);
    } else if (cmd == '0') {
        gpio_set_value(LED_GPIO, 0);          /* 输出低电平 */
        printk(KERN_INFO "[gpio_led] GPIO%d = LOW  (LED OFF)\n", LED_GPIO);
    } else {
        return -EINVAL;
    }

    return 1;
}

static ssize_t gpio_led_read(struct file *file, char __user *buf,
                              size_t len, loff_t *offset)
{
    int val;
    char status;

    if (*offset > 0)
        return 0;  /* 已经读完 */

    val = gpio_get_value(LED_GPIO);
    status = (val == 0) ? '0' : '1';

    if (copy_to_user(buf, &status, 1))
        return -EFAULT;

    *offset = 1;
    return 1;
}

static struct file_operations gpio_led_fops = {
    .owner          = THIS_MODULE,
    .open           = gpio_led_open,
    .release        = gpio_led_release,
    .write          = gpio_led_write,
    .read           = gpio_led_read,
};

/* ================================================================
 * 模块加载 / 卸载
 * ================================================================ */

static int __init gpio_led_init(void)
{
    int ret;
    dev_t dev_num;

    printk(KERN_INFO "[gpio_led] ========== 初始化 ==========\n");

    /* 1. 申请 GPIO */
    ret = gpio_request(LED_GPIO, "gpio_led");
    if (ret) {
        printk(KERN_ERR "[gpio_led] gpio_request(GPIO%d) 失败: %d\n",
               LED_GPIO, ret);
        return ret;
    }

    /* 2. 设为输出，初始低电平 */
    ret = gpio_direction_output(LED_GPIO, 0);
    if (ret) {
        printk(KERN_ERR "[gpio_led] gpio_direction_output 失败: %d\n", ret);
        goto fail_direction;
    }
    printk(KERN_INFO "[gpio_led] GPIO%d 已申请，初始=LOW\n", LED_GPIO);

    /* 3. 注册字符设备 */
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        printk(KERN_ERR "[gpio_led] alloc_chrdev_region 失败: %d\n", ret);
        goto fail_region;
    }
    major_number = MAJOR(dev_num);

    cdev_init(&gpio_led_cdev, &gpio_led_fops);
    gpio_led_cdev.owner = THIS_MODULE;
    ret = cdev_add(&gpio_led_cdev, dev_num, 1);
    if (ret < 0) {
        printk(KERN_ERR "[gpio_led] cdev_add 失败: %d\n", ret);
        goto fail_cdev;
    }

    /* 4. 创建设备节点 */
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

    printk(KERN_INFO "[gpio_led] ========== 就绪 ==========\n");
    printk(KERN_INFO "[gpio_led] 设备: /dev/%s  主设备号: %d  GPIO: %d\n",
           DEVICE_NAME, major_number, LED_GPIO);
    printk(KERN_INFO "[gpio_led] echo 1 > /dev/%s  点亮\n", DEVICE_NAME);
    printk(KERN_INFO "[gpio_led] echo 0 > /dev/%s  熄灭\n", DEVICE_NAME);
    return 0;

fail_device:
    class_destroy(gpio_led_class);
fail_class:
    cdev_del(&gpio_led_cdev);
fail_cdev:
    unregister_chrdev_region(dev_num, 1);
fail_region:
fail_direction:
    gpio_free(LED_GPIO);
    return ret;
}

static void __exit gpio_led_exit(void)
{
    dev_t dev_num = MKDEV(major_number, 0);

    printk(KERN_INFO "[gpio_led] ========== 卸载 ==========\n");

    device_destroy(gpio_led_class, dev_num);
    class_destroy(gpio_led_class);
    cdev_del(&gpio_led_cdev);
    unregister_chrdev_region(dev_num, 1);
    gpio_set_value(LED_GPIO, 0);      /* 卸载前先关灯 */
    gpio_free(LED_GPIO);              /* 释放 GPIO */

    printk(KERN_INFO "[gpio_led] GPIO%d 已释放\n", LED_GPIO);
}

module_init(gpio_led_init);
module_exit(gpio_led_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Carl");
MODULE_DESCRIPTION("RK3506 GPIO LED Driver — GPIO1_3");
MODULE_VERSION("1.0.0");
