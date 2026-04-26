# Makefile for screenshot_capture
# Usage: make all - 编译程序
#        make clean - 清理编译文件

CXX = g++
CXXFLAGS = -mwindows -O2 -Wall -Wextra -std=c++11
LDFLAGS = -lgdi32 -lcomctl32 -lgdiplus -luser32 -lkernel32

TARGET = screenshot_capture.exe
SOURCES = screenshot_capture.cpp

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES) $(LDFLAGS)
	@echo "编译完成: $(TARGET)"
	@echo "提示: 可以使用 ./$(TARGET) 运行程序"
	@echo "      ./$(TARGET) --background 以后台模式运行"
	@echo "注意: 截图将保存为JPEG格式在 C:\screenshots\"

clean:
	rm -f $(TARGET)
	@echo "清理完成"

install:
	@echo "创建安装目录..."
	@if not exist "c:\screenshots" mkdir "c:\screenshots"
	@echo "安装完成"

help:
	@echo "可用命令:"
	@echo "  make all      - 编译程序"
	@echo "  make clean    - 清理编译文件"
	@echo "  make install  - 创建截图目录"
	@echo "  make help     - 显示此帮助"

.PHONY: all clean install help