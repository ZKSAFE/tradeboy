# TradeBoy Makefile - 纯EGL硬件渲染版本
CC = gcc
CXX = g++
CFLAGS = -Wall -O2 -D_GNU_SOURCE
CXXFLAGS = $(CFLAGS) -std=c++11

# 交叉编译设置
ARM_CC = aarch64-linux-gnu-gcc
ARM_CXX = aarch64-linux-gnu-g++
ARMHF_CC = arm-linux-gnueabihf-gcc
ARMHF_CXX = arm-linux-gnueabihf-g++

# 库依赖
LIBS = -lEGL -lGLESv2 -lm -lpthread
LIBS_ARMHF = -L./lib32 -lmali -ldl -lm -lpthread
LIBS_ARMHF_GLES = -lEGL -lGLESv2 -ldl -lm -lpthread
LDFLAGS_ARMHF = -Wl,--no-as-needed -Wl,-rpath-link,./lib32

# 包含路径
INCLUDES = -I/usr/include/EGL -I/usr/include/GLES -I/usr/include/GLES2

# 源文件
DEMO_SOURCES = src/demos/sdl2demo.c
IMGUI_DEMO_SOURCES = src/demos/imgui-demo.cpp
TRADEBOY_SOURCES = \
	src/main.cpp \
	src/app/App.cpp \
	src/app/Input.cpp \
	src/market/Hyperliquid.cpp \
	src/market/HyperliquidWgetDataSource.cpp \
	src/market/HyperliquidWsDataSource.cpp \
	src/market/MarketDataService.cpp \
	src/model/TradeModel.cpp \
	src/utils/File.cpp \
	src/utils/Format.cpp \
	src/windows/NumInputWindow.cpp \
	src/spot/SpotScreen.cpp

# ImGui sources
IMGUI_DIR = third_party/imgui
IMGUI_BACKENDS_DIR = $(IMGUI_DIR)/backends
IMGUI_CORE_SOURCES = $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_tables.cpp $(IMGUI_DIR)/imgui_widgets.cpp
IMGUI_BACKEND_SOURCES = $(IMGUI_BACKENDS_DIR)/imgui_impl_sdl2.cpp $(IMGUI_BACKENDS_DIR)/imgui_impl_opengl3.cpp

BUILD_DIR_ARMHF = build/armhf
OUTPUT_DIR = output
IMGUI_DEMO_OBJS = \
	$(BUILD_DIR_ARMHF)/imgui-demo.o \
	$(BUILD_DIR_ARMHF)/imgui.o \
	$(BUILD_DIR_ARMHF)/imgui_draw.o \
	$(BUILD_DIR_ARMHF)/imgui_tables.o \
	$(BUILD_DIR_ARMHF)/imgui_widgets.o \
	$(BUILD_DIR_ARMHF)/imgui_impl_sdl2.o \
	$(BUILD_DIR_ARMHF)/imgui_impl_opengl3.o

TRADEBOY_OBJS = \
	$(patsubst src/%.cpp,$(BUILD_DIR_ARMHF)/%.o,$(TRADEBOY_SOURCES)) \
	$(BUILD_DIR_ARMHF)/imgui.o \
	$(BUILD_DIR_ARMHF)/imgui_draw.o \
	$(BUILD_DIR_ARMHF)/imgui_tables.o \
	$(BUILD_DIR_ARMHF)/imgui_widgets.o \
	$(BUILD_DIR_ARMHF)/imgui_impl_sdl2.o \
	$(BUILD_DIR_ARMHF)/imgui_impl_opengl3.o

# 目标文件
TARGET_DEMO_ARMHF = $(OUTPUT_DIR)/sdl2demo-armhf
TARGET_IMGUI_DEMO_ARMHF = $(OUTPUT_DIR)/imgui-demo-armhf
TARGET_TRADEBOY_ARMHF = $(OUTPUT_DIR)/tradeboy-armhf
DOCKER_ARMHF_BUILDER_IMAGE = rg34xx-armhf-builder:latest
CCACHE_VOLUME = -v "$(PWD)/.ccache:/ccache"

# 默认目标
all: $(TARGET_DEMO_ARMHF)

# SDL2 + OpenGL ES demo (ARMHF)
$(TARGET_DEMO_ARMHF): $(DEMO_SOURCES) | $(OUTPUT_DIR)
	$(ARMHF_CC) $(CFLAGS) -I/usr/include/SDL2 -D_REENTRANT $(DEMO_SOURCES) -o $(TARGET_DEMO_ARMHF) -L/usr/lib/arm-linux-gnueabihf $(LIBS_ARMHF_GLES) -lSDL2

# Dear ImGui demo (ARMHF)
# Note: requires Dear ImGui sources in third_party/imgui and backends.

$(OUTPUT_DIR):
	mkdir -p $(OUTPUT_DIR)

$(BUILD_DIR_ARMHF):
	mkdir -p $(BUILD_DIR_ARMHF)

$(BUILD_DIR_ARMHF)/imgui-demo.o: $(IMGUI_DEMO_SOURCES) | $(BUILD_DIR_ARMHF)
	$(ARMHF_CXX) $(CXXFLAGS) -I./src -I/usr/include/SDL2 -D_REENTRANT -DIMGUI_IMPL_OPENGL_ES2 -I./$(IMGUI_DIR) -I./$(IMGUI_BACKENDS_DIR) -c $(IMGUI_DEMO_SOURCES) -o $@

$(BUILD_DIR_ARMHF)/%.o: src/%.cpp | $(BUILD_DIR_ARMHF)
	@mkdir -p $(dir $@)
	$(ARMHF_CXX) $(CXXFLAGS) -I./src -I/usr/include/SDL2 -D_REENTRANT -DIMGUI_IMPL_OPENGL_ES2 -I./$(IMGUI_DIR) -I./$(IMGUI_BACKENDS_DIR) -c $< -o $@

$(BUILD_DIR_ARMHF)/imgui.o: $(IMGUI_DIR)/imgui.cpp | $(BUILD_DIR_ARMHF)
	$(ARMHF_CXX) $(CXXFLAGS) -DIMGUI_IMPL_OPENGL_ES2 -I./$(IMGUI_DIR) -c $< -o $@

$(BUILD_DIR_ARMHF)/imgui_draw.o: $(IMGUI_DIR)/imgui_draw.cpp | $(BUILD_DIR_ARMHF)
	$(ARMHF_CXX) $(CXXFLAGS) -DIMGUI_IMPL_OPENGL_ES2 -I./$(IMGUI_DIR) -c $< -o $@

$(BUILD_DIR_ARMHF)/imgui_tables.o: $(IMGUI_DIR)/imgui_tables.cpp | $(BUILD_DIR_ARMHF)
	$(ARMHF_CXX) $(CXXFLAGS) -DIMGUI_IMPL_OPENGL_ES2 -I./$(IMGUI_DIR) -c $< -o $@

$(BUILD_DIR_ARMHF)/imgui_widgets.o: $(IMGUI_DIR)/imgui_widgets.cpp | $(BUILD_DIR_ARMHF)
	$(ARMHF_CXX) $(CXXFLAGS) -DIMGUI_IMPL_OPENGL_ES2 -I./$(IMGUI_DIR) -c $< -o $@

$(BUILD_DIR_ARMHF)/imgui_impl_sdl2.o: $(IMGUI_BACKENDS_DIR)/imgui_impl_sdl2.cpp | $(BUILD_DIR_ARMHF)
	$(ARMHF_CXX) $(CXXFLAGS) -I/usr/include/SDL2 -D_REENTRANT -DIMGUI_IMPL_OPENGL_ES2 -I./$(IMGUI_DIR) -I./$(IMGUI_BACKENDS_DIR) -c $< -o $@

$(BUILD_DIR_ARMHF)/imgui_impl_opengl3.o: $(IMGUI_BACKENDS_DIR)/imgui_impl_opengl3.cpp | $(BUILD_DIR_ARMHF)
	$(ARMHF_CXX) $(CXXFLAGS) -DIMGUI_IMPL_OPENGL_ES2 -I./$(IMGUI_DIR) -I./$(IMGUI_BACKENDS_DIR) -c $< -o $@

$(TARGET_IMGUI_DEMO_ARMHF): $(IMGUI_DEMO_OBJS) | $(OUTPUT_DIR)
	$(ARMHF_CXX) $(CXXFLAGS) -o $(TARGET_IMGUI_DEMO_ARMHF) $(IMGUI_DEMO_OBJS) -L/usr/lib/arm-linux-gnueabihf $(LIBS_ARMHF_GLES) -lSDL2

$(TARGET_TRADEBOY_ARMHF): $(TRADEBOY_OBJS) | $(OUTPUT_DIR)
	$(ARMHF_CXX) $(CXXFLAGS) -o $(TARGET_TRADEBOY_ARMHF) $(TRADEBOY_OBJS) -L/usr/lib/arm-linux-gnueabihf $(LIBS_ARMHF_GLES) -lSDL2

# Docker ARM编译
arm-docker:
	docker run --rm -v "$(PWD):/workspace" rg34xx-sdl2-builder:latest sh -c "cd /workspace && make clean && make $(TARGET_DEMO_ARMHF)"

armhf-builder-image:
	docker build -t $(DOCKER_ARMHF_BUILDER_IMAGE) -f Dockerfile.armhf-builder .

sdl2demo-armhf-docker:
	docker run --rm -v "$(PWD):/workspace" $(CCACHE_VOLUME) $(DOCKER_ARMHF_BUILDER_IMAGE) sh -c "cd /workspace && make $(TARGET_DEMO_ARMHF) ARMHF_CC='ccache arm-linux-gnueabihf-gcc'"

imgui-demo-armhf-docker:
	docker run --rm -v "$(PWD):/workspace" $(CCACHE_VOLUME) $(DOCKER_ARMHF_BUILDER_IMAGE) sh -c "cd /workspace && make $(TARGET_IMGUI_DEMO_ARMHF) ARMHF_CXX='ccache arm-linux-gnueabihf-g++'"

tradeboy-armhf-docker:
	docker run --rm -v "$(PWD):/workspace" $(CCACHE_VOLUME) $(DOCKER_ARMHF_BUILDER_IMAGE) sh -c "cd /workspace && make $(TARGET_TRADEBOY_ARMHF) ARMHF_CXX='ccache arm-linux-gnueabihf-g++'"

output-assets: | $(OUTPUT_DIR)
	@if [ -f "NotoSansCJK-Regular.ttc" ]; then cp -f "NotoSansCJK-Regular.ttc" "$(OUTPUT_DIR)/"; fi

# 清理
clean:
	rm -f $(TARGET_DEMO_ARMHF) $(TARGET_IMGUI_DEMO_ARMHF) $(TARGET_TRADEBOY_ARMHF)
	rm -rf $(BUILD_DIR_ARMHF)

clean-obj:
	rm -rf $(BUILD_DIR_ARMHF)

# 安装到设备（脚本会根据当前目录有哪些二进制选择性上传）
install:
	./install.sh

.PHONY: all clean clean-obj arm-docker armhf-builder-image sdl2demo-armhf-docker imgui-demo-armhf-docker tradeboy-armhf-docker install
