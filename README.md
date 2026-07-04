# project_rk3506 — 正点原子 RK3506B 嵌入式 Linux 驱动开发

基于正点原子 RK3506B 开发板的 Linux 驱动学习项目，从字符设备驱动到 GPIO、中断、设备树全覆盖。

---

## 环境

| 项目 | 说明 |
|------|------|
| 开发板 | 正点原子 RK3506B（ATK-DLRK3506），ARM Cortex-A7 |
| 开发板 IP | `192.168.1.100`（网线直连），root / rk3506 |
| 开发环境 | WSL2 Ubuntu 22.04 |
| 内核源码 | `../rk3506_official`，版本 6.1.118 |
| 交叉编译器 | `arm-linux-gnueabihf-gcc` 11.4.0 |
| 部署方式 | `scp` → `ssh` → `insmod` |

## 目录结构

```
project_rk3506/
├── driver/                     ← 内核模块源码
│   ├── Makefile                ← Kbuild 编译规则（多模块）
│   ├── hello_driver.c          ← 字符设备驱动入门
│   ├── gpio_led.c              ← GPIO 输出（写死版）
│   ├── gpio_led_dt.c           ← GPIO 输出（设备树版）
│   └── button_irq.c            ← 按键中断 + 等待队列
├── test/
│   └── test_hello.c            ← 应用层测试程序
├── build_driver.sh             ← 一键交叉编译脚本
├── qemu/                       ← QEMU 测试环境（已停用）
└── src/                        ← 用户态 Hello World（旧）
```

## 驱动清单

| 驱动 | 文件 | GPIO | 知识点 |
|------|------|------|--------|
| hello_driver | `driver/hello_driver.c` | — | 字符设备框架、cdev、copy_to/from_user |
| gpio_led | `driver/gpio_led.c` | GPIO1_3 (35) | 写死 GPIO、gpio_request / gpio_set_value |
| gpio_led_dt | `driver/gpio_led_dt.c` | 设备树指定 | 标准写法、platform_driver、gpiod_get |
| button_irq | `driver/button_irq.c` | GPIO1_9 (41) | 中断、request_irq、等待队列、原子操作 |

## 快速开始

### 1. 编译

```bash
cd /mnt/e/wsl_all_projects/rk3506_embed/project_rk3506
./build_driver.sh cross
```

### 2. 部署

```bash
# 传所有模块
scp driver/hello_driver.ko root@192.168.1.100:/root
scp driver/gpio_led.ko root@192.168.1.100:/root
scp driver/button_irq.ko root@192.168.1.100:/root
scp test/test_hello root@192.168.1.100:/root

# 登录
ssh root@192.168.1.100
```

### 3. 开发板测试

```bash
# 字符设备
insmod hello_driver.ko
./test_hello
dmesg | grep hello_driver
rmmod hello_driver

# GPIO 输出（echo 1=亮, 0=灭）
insmod gpio_led.ko
echo 1 > /dev/gpio_led
cat /dev/gpio_led
echo 0 > /dev/gpio_led
rmmod gpio_led

# 按键中断（cat 阻塞等待中断）
insmod button_irq.ko
cat /proc/interrupts | grep button
cat /dev/button_irq     # 阻塞等待，中断来才返回
echo c > /dev/button_irq  # 清零计数器
rmmod button_irq
```

## 踩坑记录

| 坑 | 解决 |
|----|------|
| WSL2 insmod 报 `Invalid module format` | WSL 内核 ≠ 头文件版本，只在开发板测试 |
| 开发板 vermagic 不匹配 | 切换 .config 后必须 `modules_prepare` |
| `__stack_chk_guard` 未定义 | Makefile 加 `-fno-stack-protector` |
| 悬空 GPIO 假触发中断 | 引脚不能悬空，用杜邦线固定电平 |
| `echo > /dev/xxx` 报 Invalid argument | `echo` 带 `\n`，驱动需兼容换行符 |

## 学习笔记

Obsidian：`E:\E_ws\Obsidian_notebook\02Linux学习\Claude教学(rk3506)\`
00~09 共 10 篇，覆盖概念入门到中断驱动。
