/**
 * @file    hello_driver.c
 * @brief   Linux 内核模块入门 — 字符设备驱动框架
 * @author  Carl
 * @date    2026-06-08
 *
 * 学习要点：
 *   1. module_init() / module_exit() — 模块的入口和出口
 *   2. printk() — 内核日志打印（8 个日志级别）
 *   3. file_operations — 字符设备的核心数据结构
 *   4. cdev + class_create + device_create — 自动创建设备节点
 *   5. copy_to_user() / copy_from_user() — 内核空间与用户空间数据交换
 */

#include <linux/module.h>       /* MODULE_LICENSE, MODULE_AUTHOR, module_init/exit */
#include <linux/kernel.h>       /* printk, KERN_INFO */
#include <linux/fs.h>           /* file_operations, register_chrdev_region */
#include <linux/cdev.h>         /* cdev_init, cdev_add, cdev_del */
#include <linux/device.h>       /* class_create, device_create, class_destroy */
#include <linux/uaccess.h>      /* copy_to_user, copy_from_user */
#include <linux/slab.h>         /* kmalloc, kfree */

#define DEVICE_NAME     "hello_driver"  /* 设备名，会出现在 /dev/ 下 */
#define CLASS_NAME      "hello_class"   /* 设备类名，会出现在 /sys/class/ 下 */
#define BUFFER_SIZE     1024            /* 驱动内部缓冲区大小 */

static int major_number;                /* 主设备号（动态分配） */
static char *kernel_buffer;             /* 驱动内部缓冲区 */
static struct class *hello_class = NULL;/* 设备类 */
static struct device *hello_device = NULL;/* 设备实例 */
static struct cdev hello_cdev;          /* 字符设备结构体 */

/* ================================================================
 * file_operations 接口实现
 * ================================================================ */

/**
 * @brief 打开设备 — 应用层调用 open("/dev/hello_driver") 时触发
 */
static int hello_open(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "[hello_driver] open: 设备被打开\n");
    return 0;
}

/**
 * @brief 释放设备 — 应用层调用 close(fd) 时触发
 */
static int hello_release(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "[hello_driver] release: 设备被关闭\n");
    return 0;
}

/**
 * @brief 读设备 — 应用层调用 read(fd, buf, len) 时触发
 *
 * @return 实际读取的字节数，0 表示 EOF，负数表示错误
 *
 * 关键：必须用 copy_to_user() 把内核数据拷贝到用户空间！
 *       不能直接用 memcpy，因为用户空间指针可能无效。
 */
static ssize_t hello_read(struct file *file, char __user *buf,
                          size_t len, loff_t *offset)
{
    size_t data_len;
    int ret;

    data_len = strlen(kernel_buffer);

    /* 如果偏移量已经超过数据长度，说明读完了 */
    if (*offset >= data_len)
        return 0;

    /* 不能超过实际数据长度 */
    if (len > data_len - *offset)
        len = data_len - *offset;

    ret = copy_to_user(buf, kernel_buffer + *offset, len);
    if (ret) {
        printk(KERN_ERR "[hello_driver] read: copy_to_user 失败\n");
        return -EFAULT;
    }

    *offset += len;
    printk(KERN_INFO "[hello_driver] read: 读取了 %zu 字节\n", len);
    return len;
}

/**
 * @brief 写设备 — 应用层调用 write(fd, buf, len) 时触发
 *
 * @return 实际写入的字节数，负数表示错误
 *
 * 关键：必须用 copy_from_user() 从用户空间拷贝数据到内核！
 */
static ssize_t hello_write(struct file *file, const char __user *buf,
                           size_t len, loff_t *offset)
{
    size_t write_len;

    write_len = len < (BUFFER_SIZE - 1) ? len : (BUFFER_SIZE - 1);

    if (copy_from_user(kernel_buffer, buf, write_len))
        return -EFAULT;

    kernel_buffer[write_len] = '\0';  /* 确保字符串终止 */
    printk(KERN_INFO "[hello_driver] write: 写入 %zu 字节: \"%s\"\n",
           write_len, kernel_buffer);
    return write_len;
}

/**
 * @brief ioctl 控制接口 — 应用层调用 ioctl(fd, cmd, arg) 时触发
 *
 * 用于驱动程序特有的控制操作（如配置参数、切换模式等）
 */
static long hello_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    printk(KERN_INFO "[hello_driver] ioctl: cmd=%u, arg=%lu\n", cmd, arg);

    switch (cmd) {
    case 0:  /* 自定义命令：清空缓冲区 */
        memset(kernel_buffer, 0, BUFFER_SIZE);
        printk(KERN_INFO "[hello_driver] ioctl: 缓冲区已清空\n");
        break;
    default:
        printk(KERN_WARNING "[hello_driver] ioctl: 未知命令 %u\n", cmd);
        return -EINVAL;
    }
    return 0;
}

/* ================================================================
 * file_operations 结构体 — 将函数指针注册给 VFS
 * ================================================================
 * 这是字符设备驱动最核心的数据结构。
 * 你实现哪些函数，用户空间就能用哪些系统调用。
 */
static struct file_operations hello_fops = {
    .owner          = THIS_MODULE,      /* 防止模块在使用中被卸载 */
    .open           = hello_open,
    .release        = hello_release,
    .read           = hello_read,
    .write          = hello_write,
    .unlocked_ioctl = hello_ioctl,      /* 新版内核用 unlocked_ioctl */
};

/* ================================================================
 * 模块加载函数 — insmod / modprobe 时调用
 * ================================================================ */
static int __init hello_init(void)
{
    int ret;
    dev_t dev_num;

    printk(KERN_INFO "[hello_driver] ========== 初始化开始 ==========\n");

    /* --- 1. 分配内核缓冲区 --- */
    kernel_buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
    if (!kernel_buffer) {
        printk(KERN_ERR "[hello_driver] kmalloc 失败，内存不足\n");
        return -ENOMEM;
    }
    strcpy(kernel_buffer, "Hello RK3506! 这是驱动的默认消息。\n");
    printk(KERN_INFO "[hello_driver] 缓冲区已分配: %d 字节\n", BUFFER_SIZE);

    /* --- 2. 动态分配设备号 --- */
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        printk(KERN_ERR "[hello_driver] alloc_chrdev_region 失败: %d\n", ret);
        goto fail_region;
    }
    major_number = MAJOR(dev_num);
    printk(KERN_INFO "[hello_driver] 主设备号: %d\n", major_number);

    /* --- 3. 初始化并注册字符设备 --- */
    cdev_init(&hello_cdev, &hello_fops);
    hello_cdev.owner = THIS_MODULE;
    ret = cdev_add(&hello_cdev, dev_num, 1);
    if (ret < 0) {
        printk(KERN_ERR "[hello_driver] cdev_add 失败: %d\n", ret);
        goto fail_cdev;
    }

    /* --- 4. 创建设备类（/sys/class/hello_class/） --- */
    hello_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(hello_class)) {
        ret = PTR_ERR(hello_class);
        printk(KERN_ERR "[hello_driver] class_create 失败: %d\n", ret);
        goto fail_class;
    }

    /* --- 5. 创建设备节点（/dev/hello_driver） --- */
    hello_device = device_create(hello_class, NULL, dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(hello_device)) {
        ret = PTR_ERR(hello_device);
        printk(KERN_ERR "[hello_driver] device_create 失败: %d\n", ret);
        goto fail_device;
    }

    printk(KERN_INFO "[hello_driver] ========== 初始化成功 ==========\n");
    printk(KERN_INFO "[hello_driver] 设备节点: /dev/%s  主设备号: %d\n",
           DEVICE_NAME, major_number);
    return 0;

    /* --- 错误回滚路径 --- */
fail_device:
    class_destroy(hello_class);
fail_class:
    cdev_del(&hello_cdev);
fail_cdev:
    unregister_chrdev_region(dev_num, 1);
fail_region:
    kfree(kernel_buffer);
    return ret;
}

/* ================================================================
 * 模块卸载函数 — rmmod 时调用
 * ================================================================ */
static void __exit hello_exit(void)
{
    dev_t dev_num = MKDEV(major_number, 0);

    printk(KERN_INFO "[hello_driver] ========== 卸载开始 ==========\n");

    /* 销毁顺序：与初始化相反（后创建的先销毁） */
    device_destroy(hello_class, dev_num);   /* 删除 /dev/hello_driver */
    class_destroy(hello_class);             /* 删除 /sys/class/hello_class */
    cdev_del(&hello_cdev);                  /* 注销字符设备 */
    unregister_chrdev_region(dev_num, 1);   /* 释放设备号 */
    kfree(kernel_buffer);                   /* 释放缓冲区 */

    printk(KERN_INFO "[hello_driver] ========== 卸载完成 ==========\n");
}

/* ================================================================
 * 模块元信息
 * ================================================================ */
module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");                              /* 许可证类型 */
MODULE_AUTHOR("Carl");                              /* 作者 */
MODULE_DESCRIPTION("RK3506 字符设备驱动入门示例");    /* 描述 */
MODULE_VERSION("1.0.0");                            /* 版本 */
