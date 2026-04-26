#!/bin/bash
# build.sh - MSYS2环境编译脚本

echo "========================================="
echo "截图捕获程序 - MSYS2编译脚本 (JPEG版本)"
echo "========================================="

# 检查是否在MSYS2环境下
if [[ ! "$MSYSTEM" =~ MINGW ]]; then
    echo "警告: 建议在MSYS2 MinGW环境下运行此脚本"
    echo "请使用 'MSYS2 MinGW 64-bit' 或 'MSYS2 MinGW 32-bit'"
fi

# 确保安装了必要的包
echo "检查依赖包..."
pacman -Q mingw-w64-x86_64-gcc &>/dev/null || echo "请安装: pacman -S mingw-w64-x86_64-gcc make"

# 清理旧文件
echo "清理旧文件..."
make clean

# 编译程序
echo "开始编译..."
make all

# 检查编译结果
if [ -f "screenshot_capture.exe" ]; then
    echo ""
    echo "✓ 编译成功！"
    echo "生成文件: screenshot_capture.exe"
    echo ""
    echo "新功能："
    echo "  • 单实例运行 - 再次运行会激活已有窗口"
    echo "  • JPEG格式保存 - 文件更小，质量85%"
    echo "  • 双热键支持 - Ctrl+PrtSc 和 Ctrl+F1"
    echo ""
    echo "使用方法："
    echo "  前台模式: ./screenshot_capture.exe"
    echo "  后台模式: ./screenshot_capture.exe --background"
    echo "  或: ./screenshot_capture.exe -b"
    echo ""
    echo "截图将保存在: C:\screenshots\ 目录，格式为 JPEG"
    
    # 创建截图目录
    mkdir -p /c/screenshots
    echo "截图目录已创建: C:\screenshots"
else
    echo ""
    echo "✗ 编译失败，请检查错误信息"
    exit 1
fi