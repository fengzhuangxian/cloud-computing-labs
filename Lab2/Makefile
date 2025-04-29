# 定义编译器和通用编译选项
CXX = g++
CXXFLAGS += -O2 -ggdb -DDEBUG
CXXFLAGS += -Wall -Wextra

# 定义目标文件列表
OBJS = server.o http.o post.o get.o

# 定义最终的可执行文件名
TARGET = http-server

# 新增 Proxy-server 可执行文件的目标文件
PROXY_OBJS = Proxy-server.o

# 新增 Proxy-server 可执行文件名
PROXY_TARGET = Proxy-server

# 默认目标，生成可执行文件
all: $(TARGET) $(PROXY_TARGET)

# 可执行文件的生成规则，依赖于所有目标文件
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ -lpthread

$(PROXY_TARGET): $(PROXY_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ -lpthread

# 目标文件的生成规则，根据源文件自动生成
%.o: %.c
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 清理规则，删除所有目标文件和可执行文件
clean:
	rm -f $(OBJS) $(TARGET) $(PROXY_OBJS) $(PROXY_TARGET)

