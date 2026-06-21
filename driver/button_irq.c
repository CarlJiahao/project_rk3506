/**
 * @file    button_irq.c
 * @brief   按键中断驱动 — GPIO 输入 + 中断处理
 * @author  Carl
 * @date    2026-06-21
 *
 * 学习要点：
 *   1. gpio_direction_input()  — 设置 GPIO 为输入模式
 *   2. gpio_to_irq()           — GPIO 编号 → 中断号
 *   3. request_irq() / free_irq() — 注册/注销中断处理函数
 *   4. 中断上下文注意事项       — 不能 sleep、不能 copy_to_user
 *   5. wait_queue              — 让 read 阻塞，等中断来了才返回
 *
 * 引脚: GPIO1_9 (全局编号 41)，设为输入，下降沿触发
 * 用法: cat /dev/button_irq  会阻塞，收到中断后返回按键次数
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>       /* request_irq, free_irq */
#include <linux/wait.h>           /* wait_queue_head_t */
#include <linux/sched.h>          /* schedule() */
#include <linux/slab.h>

#define DEVICE_NAME     "button_irq"
#define CLASS_NAME      "button_irq_class"
#define BUTTON_GPIO     41       /* GPIO1_9 — 全局编号 32+9=41 */
#define DEBOUNCE_MS     50       /* 防抖 50ms */

static int major_number;
static struct class *button_class = NULL;
static struct device *button_device = NULL;
static struct cdev button_cdev;

static int irq_number;                       /* GPIO 对应的中断号 */
static atomic_t press_count = ATOMIC_INIT(0);/* 按键次数（原子操作） */
static DECLARE_WAIT_QUEUE_HEAD(button_wq);    /* 等待队列 */

/* ================================================================
 * 中断处理函数（ISR — Interrupt Service Routine）
 * ================================================================
 *
 * ⚠️ 中断上下文限制：
 *   - 不能调用 copy_to_user() / copy_from_user()
 *   - 不能调用可能 sleep 的函数（比如 mutex_lock）
 *   - 不能用 GFP_KERNEL 分配内存
 *   - 只能做简短操作，尽快返回
 */
static irqreturn_t button_isr(int irq, void *dev_id)
{
    atomic_inc(&press_count);                     /* 按键次数 +1 */
    printk(KERN_INFO "[button_irq] 按键触发！第 %d 次\n",
           atomic_read(&press_count));
    wake_up_interruptible(&button_wq);            /* 唤醒等待的 read */
    return IRQ_HANDLED;
}

/* ================================================================
 * file_operations
 * ================================================================ */

static int button_open(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "[button_irq] open: 等待按键...\n");
    return 0;
}

static int button_release(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "[button_irq] release\n");
    return 0;
}

/**
 * @brief 读 — 阻塞等待按键中断
 *
 * 应用层调用 read() 时：
 *   如果没有按键 → 进程休眠，释放 CPU
 *   按键中断来了 → ISR 唤醒进程 → read 返回按键次数
 */
static ssize_t button_read(struct file *file, char __user *buf,
                            size_t len, loff_t *offset)
{
    char msg[32];
    int count, ret;

    if (*offset > 0)
        return 0;  /* EOF — 读完一次就结束 */

    /* 等待中断（阻塞，不占 CPU） */
    ret = wait_event_interruptible(button_wq,
                    atomic_read(&press_count) > 0);
    if (ret)
        return ret;  /* 被信号打断 */

    count = atomic_read(&press_count);
    snprintf(msg, sizeof(msg), "按键次数: %d\n", count);

    if (copy_to_user(buf, msg, strlen(msg)))
        return -EFAULT;

    *offset = strlen(msg);
    return strlen(msg);
}

static ssize_t button_write(struct file *file, const char __user *buf,
                             size_t len, loff_t *offset)
{
    char cmd;

    if (copy_from_user(&cmd, buf, 1))
        return -EFAULT;

    if (cmd == 'c' || cmd == 'C') {
        atomic_set(&press_count, 0);    /* 清零计数器 */
        printk(KERN_INFO "[button_irq] 计数器已清零\n");
    }
    return len;
}

static struct file_operations button_fops = {
    .owner   = THIS_MODULE,
    .open    = button_open,
    .release = button_release,
    .read    = button_read,
    .write   = button_write,
};

/* ================================================================
 * 模块加载 / 卸载
 * ================================================================ */

static int __init button_init(void)
{
    int ret;
    dev_t dev_num;

    printk(KERN_INFO "[button_irq] ========== 初始化 ==========\n");

    /* 1. 申请 GPIO */
    ret = gpio_request(BUTTON_GPIO, "button_irq");
    if (ret) {
        printk(KERN_ERR "[button_irq] gpio_request 失败\n");
        return ret;
    }

    /* 2. 设为输入 */
    ret = gpio_direction_input(BUTTON_GPIO);
    if (ret) {
        printk(KERN_ERR "[button_irq] gpio_direction_input 失败\n");
        goto fail_input;
    }

    /* 3. GPIO → 中断号 */
    irq_number = gpio_to_irq(BUTTON_GPIO);
    if (irq_number < 0) {
        ret = irq_number;
        printk(KERN_ERR "[button_irq] gpio_to_irq 失败\n");
        goto fail_irq_map;
    }
    printk(KERN_INFO "[button_irq] GPIO%d → IRQ%d\n", BUTTON_GPIO, irq_number);

    /* 4. 注册中断处理函数
     *   参数：
     *     irq_number     — 中断号
     *     button_isr     — 中断处理函数
     *     IRQF_TRIGGER_FALLING — 下降沿触发（高→低）
     *     "button_irq"   — 中断名称（/proc/interrupts 里看到）
     *     NULL           — dev_id（可传自定义数据）
     */
    ret = request_irq(irq_number, button_isr,
                      IRQF_TRIGGER_FALLING,
                      "button_irq", NULL);
    if (ret) {
        printk(KERN_ERR "[button_irq] request_irq 失败: %d\n", ret);
        goto fail_irq;
    }
    printk(KERN_INFO "[button_irq] 中断已注册，触发方式: 下降沿\n");

    /* 5. 注册字符设备 */
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) goto fail_region;
    major_number = MAJOR(dev_num);

    cdev_init(&button_cdev, &button_fops);
    button_cdev.owner = THIS_MODULE;
    ret = cdev_add(&button_cdev, dev_num, 1);
    if (ret < 0) goto fail_cdev;

    button_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(button_class)) { ret = PTR_ERR(button_class); goto fail_class; }

    button_device = device_create(button_class, NULL, dev_num,
                                   NULL, DEVICE_NAME);
    if (IS_ERR(button_device)) { ret = PTR_ERR(button_device); goto fail_device; }

    printk(KERN_INFO "[button_irq] ========== 就绪 ==========\n");
    printk(KERN_INFO "[button_irq] /dev/%s  — cat 它来等待按键\n", DEVICE_NAME);
    return 0;

    /* 错误回滚 */
fail_device: class_destroy(button_class);
fail_class:  cdev_del(&button_cdev);
fail_cdev:   unregister_chrdev_region(dev_num, 1);
fail_region:
fail_irq:
fail_irq_map:
fail_input:  gpio_free(BUTTON_GPIO);
    return ret;
}

static void __exit button_exit(void)
{
    dev_t dev_num = MKDEV(major_number, 0);

    printk(KERN_INFO "[button_irq] ========== 卸载 ==========\n");

    device_destroy(button_class, dev_num);
    class_destroy(button_class);
    cdev_del(&button_cdev);
    unregister_chrdev_region(dev_num, 1);
    free_irq(irq_number, NULL);        /* 注销中断 */
    gpio_free(BUTTON_GPIO);            /* 释放 GPIO */

    printk(KERN_INFO "[button_irq] IRQ%d 已释放\n", irq_number);
}

module_init(button_init);
module_exit(button_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Carl");
MODULE_DESCRIPTION("RK3506 Button Interrupt Driver — GPIO1_9");
MODULE_VERSION("1.0.0");
