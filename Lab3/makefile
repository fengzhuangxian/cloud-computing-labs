# Makefile for Raft_refactor

CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -pthread
LDFLAGS := -pthread

# 编译目标
TARGET := kvstoreraftsystem

# 源文件目录
SRC_DIR := src
BUILD_DIR := build

# 查找所有源文件
SRCS := $(shell find $(SRC_DIR) -name "*.cpp")
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

# 头文件目录
INCLUDES := -I$(SRC_DIR)

# 默认目标
all: $(TARGET)

# 编译可执行文件
$(TARGET): $(OBJS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ -o $@

# 编译源文件
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

# 包含依赖关系
-include $(DEPS)

# 清理中间文件
clean:
	rm -rf $(BUILD_DIR) $(TARGET)

# 支持make后动态识别源码文件结构
$(shell mkdir -p $(sort $(dir $(OBJS))))

.PHONY: all clean 