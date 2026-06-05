# 编译器设置
CC = gcc
CFLAGS = -Iinclude  # 指定头文件搜索路径
LDFLAGS = -Llib -lm  # 库文件路径与链接库

# 目录定义
SRC_DIR = src
INC_DIR = include
BUILD_DIR = build

# 自动获取所有.c文件
SRCS = $(wildcard $(SRC_DIR)/*.c)
# 生成对应的.o文件路径
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))

# 目标文件
TARGET = $(BUILD_DIR)/app

# 默认目标
all: $(TARGET)

# 链接生成可执行文件
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

# 编译生成.o文件
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)  # 确保build目录存在
	$(CC) $(CFLAGS) -c $< -o $@

# 清理规则
clean:
	rm -rf $(BUILD_DIR)/*

# 伪目标
.PHONY: all clean