# ============================================================
# 内核模块 Makefile（Kbuild 体系）
# ============================================================
# 使用方式：
#   本机编译:   make
#   交叉编译:   make CROSS=1 KERNEL_SRC=/path/to/kernel
#   清理:       make clean
#
# 前置条件：
#   本机编译需要安装 linux-headers-$(uname -r)
#   交叉编译需要指定 KERNEL_SRC（已编译过的内核源码树）
# ============================================================

# --- 模块名（必须和 .c 文件名一致！） ---
obj-m := hello_driver.o

# --- 检测交叉编译 ---
ifeq ($(CROSS),1)
    # 交叉编译：需指定内核源码路径和交叉工具链前缀
    # 例：make CROSS=1 KERNEL_SRC=~/rk3506-kernel ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-
    ARCH            ?= arm
    CROSS_COMPILE   ?= arm-linux-gnueabihf-
    export ARCH
    export CROSS_COMPILE
else
    # 本机编译：使用当前运行的内核
    KERNEL_SRC ?= $(firstword $(wildcard /lib/modules/*/build))
endif

# --- 编译目标 ---
all:
	@echo "==> 编译内核模块: $(obj-m)"
	@echo "    内核源码路径: $(KERNEL_SRC)"
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules

# --- 清理 ---
clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean
	rm -f *.o *.ko *.mod.c *.mod *.order *.symvers

# --- 信息查看 ---
info:
	@echo "模块名:      $(obj-m)"
	@echo "内核源码:    $(KERNEL_SRC)"
	@echo "架构:        $(ARCH)"
	@echo "交叉编译器:  $(CROSS_COMPILE)"

.PHONY: all clean info
