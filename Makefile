# TradeBoy Makefile - 纯EGL硬件渲染版本
CC = gcc
CXX = g++
CFLAGS = -Wall -O2 -D_GNU_SOURCE
CXXFLAGS = $(CFLAGS) -std=c++11

# 交叉编译设置
ARM_CC = aarch64-linux-gnu-gcc
ARM_CXX = aarch64-linux-gnu-g++
ARMHF_CC = arm-linux-gnueabihf-gcc

# 库依赖
LIBS = -lEGL -lGLESv2 -lm -lpthread
LIBS_ARMHF = -L./lib32 -lmali -ldl -lm -lpthread
LDFLAGS_ARMHF = -Wl,--no-as-needed -Wl,-rpath-link,./lib32

# 包含路径
INCLUDES = -I/usr/include/EGL -I/usr/include/GLES -I/usr/include/GLES2

# 源文件
SOURCES = src/tradeboy-main.c
DEMO_SOURCES = src/sdl2demo.c

# 目标文件
TARGET_MAC = tradeboy-mac
TARGET_ARM = tradeboy-arm
TARGET_ARMHF = tradeboy-armhf
TARGET_DEMO_ARMHF = sdl2demo-armhf

# 默认目标
all: $(TARGET_MAC)

# Mac版本
$(TARGET_MAC): $(SOURCES)
	$(CC) $(CFLAGS) $(INCLUDES) $(SOURCES) -o $(TARGET_MAC) $(LIBS)

# ARM交叉编译版本
$(TARGET_ARM): $(SOURCES)
	$(ARM_CC) $(CFLAGS) $(INCLUDES) $(SOURCES) -o $(TARGET_ARM) $(LIBS)

# ARMHF交叉编译版本 (32-bit, for /usr/lib32 Mali stack)
$(TARGET_ARMHF): $(SOURCES)
	$(ARMHF_CC) $(CFLAGS) $(INCLUDES) $(SOURCES) -o $(TARGET_ARMHF) $(LIBS_ARMHF) $(LDFLAGS_ARMHF)

# SDL2 + OpenGL ES demo (ARMHF)
$(TARGET_DEMO_ARMHF): $(DEMO_SOURCES)
	$(ARMHF_CC) $(CFLAGS) -I/usr/include/SDL2 -D_REENTRANT $(DEMO_SOURCES) -o $(TARGET_DEMO_ARMHF) -L/usr/lib/arm-linux-gnueabihf $(LIBS_ARMHF) -lSDL2 $(LDFLAGS_ARMHF)

# Docker ARM编译
arm-docker:
	docker run --rm -v "$(PWD):/workspace" rg34xx-sdl2-builder:latest sh -c "cd /workspace && make clean && make $(TARGET_ARM) CC=aarch64-linux-gnu-gcc"

# Docker ARMHF编译 (installs armhf toolchain at runtime; does NOT build a new image)
armhf-docker:
	docker run --rm -v "$(PWD):/workspace" rg34xx-sdl2-builder:latest sh -c "set -e; apt-get update; apt-get install -y gcc-arm-linux-gnueabihf; cd /workspace; make $(TARGET_ARMHF)"

sdl2demo-armhf-docker:
	docker run --rm -v "$(PWD):/workspace" rg34xx-sdl2-builder:latest sh -c "set -e; dpkg --add-architecture armhf; apt-get update; apt-get install -y gcc-arm-linux-gnueabihf libsdl2-dev:armhf; cd /workspace; make $(TARGET_DEMO_ARMHF)"

# 清理
clean:
	rm -f $(TARGET_MAC) $(TARGET_ARM) $(TARGET_ARMHF) $(TARGET_DEMO_ARMHF)

# 安装到设备
install: $(TARGET_ARM)
	./install.sh

install-armhf: $(TARGET_ARMHF)
	./install.sh

# 运行
run: $(TARGET_ARM)
	./tradeboy-start.sh

run-armhf: $(TARGET_ARMHF)
	./tradeboy-start.sh

run-sdl2demo-armhf: $(TARGET_DEMO_ARMHF)
	./sdl2demo-start.sh

.PHONY: all clean arm-docker armhf-docker sdl2demo-armhf-docker install install-armhf run run-armhf run-sdl2demo-armhf
