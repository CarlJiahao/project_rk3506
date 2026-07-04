/**
 * @file    test_hello.c
 * @brief   应用层测试程序 — 调用 hello_driver 字符设备驱动
 * @author  Carl
 * @date    2026-06-08
 *
 * 学习要点：
 *   1. open/read/write/ioctl/close — 标准 POSIX 文件操作接口
 *   2. 用户空间通过设备节点与内核驱动交互
 *   3. 驱动"三件套"：设备树 + 驱动代码 + 应用测试（缺一不可）
 *
 * 编译（本机）：
 *   gcc -o test_hello test_hello.c
 *
 * 编译（交叉）：
 *   arm-linux-gnueabihf-gcc -o test_hello test_hello.c
 *
 * 运行（需先加载驱动）：
 *   sudo insmod ../driver/hello_driver.ko
 *   sudo ./test_hello
 *   sudo rmmod hello_driver
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>      /* open */
#include <unistd.h>     /* read, write, close */
#include <sys/ioctl.h>  /* ioctl */
#include <errno.h>      /* errno */

#define DEVICE_PATH     "/dev/hello_driver"
#define IOCTL_CLEAR     0   /* 必须与驱动中定义的命令一致 */

int main(void)
{
    int fd;
    char read_buf[1024] = {0};
    char write_buf[] = "Hello from userspace! 用户空间发来问候~\n";
    ssize_t ret;

    printf("========================================\n");
    printf("  RK3506 字符设备驱动 - 应用层测试\n");
    printf("========================================\n\n");

    /* --- 1. 打开设备 --- */
    printf("[1/5] 打开设备 %s ...\n", DEVICE_PATH);
    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("  open 失败");
        fprintf(stderr, "\n提示：请先加载驱动模块！\n");
        fprintf(stderr, "  sudo insmod ../driver/hello_driver.ko\n");
        return EXIT_FAILURE;
    }
    printf("  成功！fd = %d\n\n", fd);

    /* --- 2. 读取默认消息 --- */
    printf("[2/5] 读取驱动默认消息 ...\n");
    memset(read_buf, 0, sizeof(read_buf));
    ret = read(fd, read_buf, sizeof(read_buf) - 1);
    if (ret < 0) {
        perror("  read 失败");
        close(fd);
        return EXIT_FAILURE;
    }
    printf("  读取了 %zd 字节:\n", ret);
    printf("  ┌─────────────────────────────────────┐\n");
    printf("  │ %s", read_buf);
    printf("  └─────────────────────────────────────┘\n\n");

    /* --- 3. 写入数据 --- */
    printf("[3/5] 向驱动写入数据 ...\n");
    ret = write(fd, write_buf, strlen(write_buf));
    if (ret < 0) {
        perror("  write 失败");
        close(fd);
        return EXIT_FAILURE;
    }
    printf("  写入了 %zd 字节: \"%s\"\n", ret, write_buf);

    /* --- 4. 回读验证 --- */
    printf("[4/5] 回读验证刚才写入的内容 ...\n");
    memset(read_buf, 0, sizeof(read_buf));
    ret = read(fd, read_buf, sizeof(read_buf) - 1);
    if (ret < 0) {
        perror("  read 失败");
        close(fd);
        return EXIT_FAILURE;
    }
    printf("  读取了 %zd 字节:\n", ret);
    printf("  ┌─────────────────────────────────────┐\n");
    printf("  │ %s", read_buf);
    printf("  └─────────────────────────────────────┘\n\n");

    /* --- 5. ioctl 控制操作 --- */
    printf("[5/5] ioctl 清空缓冲区 ...\n");
    ret = ioctl(fd, IOCTL_CLEAR);
    if (ret < 0) {
        perror("  ioctl 失败");
        close(fd);
        return EXIT_FAILURE;
    }
    printf("  成功！\n\n");

    /* --- 关闭设备 --- */
    close(fd);
    printf("========================================\n");
    printf("  测试完成！查看内核日志:\n");
    printf("  dmesg | tail -20\n");
    printf("========================================\n");

    return EXIT_SUCCESS;
}
