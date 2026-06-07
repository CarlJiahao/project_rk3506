#!/bin/bash
# ============================================================
# build_driver.sh — 一键编译驱动 + 测试程序
# ============================================================
# 用法：
#   ./build_driver.sh          本机编译（WSL2/Linux）
#   ./build_driver.sh cross    交叉编译（ARM/RK3506）
#   ./build_driver.sh clean    清理所有编译产物
#   ./build_driver.sh load     加载驱动（需 sudo）
#   ./build_driver.sh unload   卸载驱动（需 sudo）
#   ./build_driver.sh test     编译 + 加载 + 测试 + 卸载（一键全流程）
# ============================================================

set -e

# --- 交叉编译配置 ---
CROSS_COMPILER=arm-linux-gnueabihf-
KERNEL_SRC_RK3506=~/rk3506_official   # RK3506 内核源码路径（经 make defconfig 配置过）

# 自动查找内核构建目录（兼容 WSL2：运行内核 vs 头文件版本可能不同）
KERNEL_SRC_DEFAULT=$(ls -d /lib/modules/*/build 2>/dev/null | head -1)
if [ -z "$KERNEL_SRC_DEFAULT" ]; then
    KERNEL_SRC_DEFAULT=/lib/modules/$(uname -r)/build
fi

# --- 颜色输出 ---
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }

# --- 编译内核模块 ---
build_module() {
    local mode=$1
    info "编译内核模块 (hello_driver.ko) ..."

    cd driver

    if [ "$mode" == "cross" ]; then
        # 检查 RK3506 内核源码是否存在
        if [ ! -f "$KERNEL_SRC_RK3506/Makefile" ]; then
            error "RK3506 内核源码未找到: $KERNEL_SRC_RK3506"
            exit 1
        fi
        make CROSS=1 KERNEL_SRC="$KERNEL_SRC_RK3506"
    else
        # 检查内核构建目录是否存在
        if [ ! -d "$KERNEL_SRC_DEFAULT" ]; then
            error "内核头文件未安装！请先执行:"
            echo "  sudo apt-get install linux-headers-\$(uname -r)"
            exit 1
        fi
        make
    fi

    cd ..
    info "内核模块编译完成: driver/hello_driver.ko"
}

# --- 编译测试程序 ---
build_test() {
    local mode=$1
    info "编译测试程序 (test_hello) ..."

    cd test
    if [ "$mode" == "cross" ]; then
        ${CROSS_COMPILER}gcc -static -o test_hello test_hello.c
    else
        gcc -o test_hello test_hello.c
    fi
    cd ..

    info "测试程序编译完成: test/test_hello"
}

# --- 清理 ---
do_clean() {
    info "清理编译产物 ..."
    cd driver && make clean 2>/dev/null; cd ..
    rm -f test/test_hello
    info "清理完成"
}

# --- 加载驱动 ---
do_load() {
    if [ ! -f driver/hello_driver.ko ]; then
        error "驱动文件不存在，请先编译！"
        exit 1
    fi
    info "加载驱动模块 ..."
    sudo insmod driver/hello_driver.ko
    info "加载成功！查看: lsmod | grep hello_driver"
    lsmod | grep hello_driver
    echo ""
    info "设备节点:"
    ls -la /dev/hello_driver 2>/dev/null || warn "设备节点未自动创建（可能需要 udev）"
}

# --- 卸载驱动 ---
do_unload() {
    info "卸载驱动模块 ..."
    sudo rmmod hello_driver 2>/dev/null || warn "模块未加载或已卸载"
    info "卸载完成"
}

# --- 查看内核日志 ---
do_log() {
    info "最近的内核日志:"
    dmesg | grep hello_driver | tail -20
}

# --- 一键测试 ---
do_test() {
    info "========== 一键测试全流程 =========="
    echo ""

    # 1. 编译
    build_module local
    build_test local

    # 2. 加载驱动
    do_load

    # 3. 运行测试
    info "运行测试程序 ..."
    sudo ./test/test_hello

    # 4. 查看内核日志
    echo ""
    do_log

    # 5. 卸载
    echo ""
    do_unload

    info "========== 测试完成 =========="
}

# ============================================================
# 主入口
# ============================================================
case "${1:-all}" in
    all)
        build_module local
        build_test local
        info "全部编译完成！"
        echo ""
        echo "下一步："
        echo "  ./build_driver.sh load    加载驱动"
        echo "  ./build_driver.sh test    一键测试"
        echo "  ./build_driver.sh unload  卸载驱动"
        echo "  dmesg | grep hello_driver 查看内核日志"
        ;;
    cross)
        build_module cross
        build_test cross
        info "交叉编译完成！将 driver/hello_driver.ko 和 test/test_hello 拷贝到开发板"
        ;;
    clean)
        do_clean
        ;;
    load)
        do_load
        ;;
    unload)
        do_unload
        ;;
    log)
        do_log
        ;;
    test)
        do_test
        ;;
    *)
        echo "用法: $0 {all|cross|clean|load|unload|log|test}"
        echo ""
        echo "  all     本机编译驱动 + 测试程序（默认）"
        echo "  cross   交叉编译（ARM）"
        echo "  clean   清理编译产物"
        echo "  load    加载驱动模块"
        echo "  unload  卸载驱动模块"
        echo "  log     查看内核日志"
        echo "  test    编译 + 加载 + 测试 + 卸载（一键全流程）"
        exit 1
        ;;
esac
