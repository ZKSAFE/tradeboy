# TradeBoy Makefile
CC = gcc
CFLAGS = -Wall -O2 -D_GNU_SOURCE
SDL_CFLAGS = $(shell pkg-config --cflags sdl2 SDL2_ttf)
SDL_LIBS = $(shell pkg-config --libs sdl2 SDL2_ttf)
TARGET_MAC = tradeboy-mac
TARGET_ARM = tradeboy-arm
SRC_DIR = src
SOURCES = $(SRC_DIR)/tradeboy-main.c

# Mac版本
sdl2-mac: $(TARGET_MAC)

$(TARGET_MAC): $(SOURCES)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(SOURCES) -o $(TARGET_MAC) $(SDL_LIBS) -lm

# ARM版本（交叉编译）
sdl2-arm: $(TARGET_ARM)

$(TARGET_ARM): $(SOURCES)
	aarch64-linux-gnu-gcc $(CFLAGS) -D__CROSS_COMPILE__ $(SDL_CFLAGS) $(SOURCES) -o $(TARGET_ARM) $(SDL_LIBS) -lm

# 清理
clean:
	rm -f $(TARGET_MAC) $(TARGET_ARM)

# 安装依赖
deps:
	@if [ "$(shell uname)" = "Darwin" ]; then \
		brew install sdl2 sdl2_ttf; \
	else \
		sudo apt-get update && sudo apt-get install -y libsdl2-dev libsdl2-ttf-dev; \
	fi

# Docker构建
docker-build:
	docker build -t tradeboy-builder -f Dockerfile.tradeboy .
	docker run --rm -v $(PWD):/output tradeboy-builder cp tradeboy-arm /output/

# 帮助
help:
	@echo "TradeBoy Build System"
	@echo "====================="
	@echo "make sdl2-mac    - Build for macOS"
	@echo "make sdl2-arm    - Cross-compile for ARM"
	@echo "make clean       - Clean build files"
	@echo "make deps        - Install dependencies"
	@echo "make docker-build - Build using Docker"
	@echo "make help        - Show this help"

.PHONY: sdl2-mac sdl2-arm clean deps docker-build help
