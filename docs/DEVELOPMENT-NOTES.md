# TradeBoy / RG34XX 开发与部署说明

## 项目定位

本目录用于 RG34XX 掌机上的 SDL2 + OpenGL ES（Mali）应用实验与验证。

当前可用产物：

- `imgui-demo-armhf`：Dear ImGui demo（SDL2 + OpenGL ES），用于验证显示链路、输入事件、网络测试弹窗。
- `sdl2demo-armhf`：最小 SDL2 + OpenGL ES demo（红/绿/蓝切换），用于验证是否能正常上屏。
- `tradeboy-ui-demo-armhf`：TradeBoy UI standalone demo（SDL2 + OpenGL ES + ImGui），用于快速验证 UI 渲染与布局（不对接 API）。

## 运行环境（掌机端）

### 设备信息

- 设备：Anbernic RG34XX
- 系统：Linux 4.9.170（aarch64）
- framebuffer：`/dev/fb0`

### 关键点：32-bit Mali 用户态栈

RG34XX 的 Mali EGL/GLES 用户态栈位于：

- `/usr/lib32`

因此：

- 需要构建 **32-bit armhf** 可执行文件（`arm-linux-gnueabihf-*`）
- 运行时常用：

```sh
export LD_LIBRARY_PATH=/usr/lib32:/usr/lib:/mnt/vendor/lib
```

### Demo 运行命令

ImGui demo：

```sh
ssh root@192.168.1.7 'cd /mnt/mmc/Roms/APPS && ./imgui-demo-armhf'
```

SDL2 demo：

```sh
ssh root@192.168.1.7 'cd /mnt/mmc/Roms/APPS && ./sdl2demo-armhf'
```

## 依赖库（运行时 / 构建时）

### 掌机运行时依赖

- SDL2（系统自带）
- EGL / GLESv2（由 Mali 驱动栈提供，通常在 `/usr/lib32`）
- 标准库：`libdl` / `libm` / `pthread`

### Docker 交叉编译依赖（armhf）

在 builder 镜像中预装：

- `gcc-arm-linux-gnueabihf`
- `g++-arm-linux-gnueabihf`
- `libsdl2-dev:armhf`
- `ccache`

ImGui 依赖以 submodule 形式引入：

- `third_party/imgui`

## 编译方式（推荐流程）

### 1) 首次准备：构建 armhf builder 镜像（只需一次）

```sh
make armhf-builder-image
```

该镜像会缓存 `apt-get` 安装的交叉编译工具链与 `libsdl2-dev:armhf`，避免每次编译重复下载依赖。

### 2) 日常编译：ImGui demo

```sh
make imgui-demo-armhf-docker
```

### 3) 日常编译：SDL2 demo

```sh
make sdl2demo-armhf-docker
```

### 4) 部署到掌机

```sh
make install
```

`install.sh` 会按当前目录存在的可执行文件选择性上传：

- `imgui-demo-armhf`
- `sdl2demo-armhf`
- `tradeboy-ui-demo-armhf`
- `NotoSansCJK-Regular.ttc`（存在就传）

### 5) 日常编译：TradeBoy UI demo

```sh
make tradeboy-ui-demo-armhf-docker
```

## Docker 编译提速策略

### 问题：每次 docker run 都 apt-get

最耗时的是：

- `apt-get update`
- `apt-get install`

解决：

- 新增 `Dockerfile.armhf-builder`
- `make armhf-builder-image` 一次性安装依赖
- 后续编译目标直接使用预装镜像

### 问题：ccache 不生效（单条 g++ 编译全部 cpp）

如果用一条命令把多个 `.cpp` 一起编译+链接，`ccache` 基本命中率很差。

解决：

- 将 `imgui-demo` 改为分文件编译（`.cpp -> .o`）再链接
- 并挂载宿主机缓存目录：`tradeboy/.ccache -> /ccache`

经验结果（示例）：

- 首次（清理 obj 后编译）：约 10s 级
- 二次（无代码改动）：约 0.6s（接近 Docker 启动+判定 up-to-date 的极限）

## 输入与按键映射（RG34XX）

### 手柄按钮（SDL_JOYBUTTONDOWN/UP）

基于 `rg34xx-native-app` 的映射：

- 0：A
- 1：B
- 2：Y（X/Y 交换）
- 3：X（X/Y 交换）
- 4：L1
- 5：R1
- 6：SELECT
- 7：START
- 8：M
- 9：L2
- 10：R2
- 13：VOL-
- 14：VOL+

### 方向键（SDL_JOYHATMOTION）

实际设备上 Hat value 与方向关系：

- 0：CENTER
- 1：UP
- 2：RIGHT
- 3：RIGHT+UP
- 4：DOWN
- 6：RIGHT+DOWN
- 8：LEFT
- 9：LEFT+UP
- 12：LEFT+DOWN

### 键盘事件

设备上观察到键盘事件基本只有：

- 关机键（`SDLK_POWER`）

## 开发过程与踩坑记录

### 坑 1：EGL/GLES 初始化成功但黑屏

现象：

- `GL_RENDERER` 能拿到 `Mali-G31`
- 渲染循环有 FPS
- 但屏幕黑

关键结论：

- “EGL 渲染成功”不等价于“画面上屏”
- RG34XX 上更可靠的显示提交链路是：
  - SDL2 创建 window + GLES context
  - `SDL_GL_SwapWindow()` 提交

因此新增 `sdl2demo-armhf` 验证 SDL2 链路，确认可见。

### 坑 2：强行设置 `SDL_VIDEODRIVER=sdl2` 会导致 SDL_Init 失败

现象：

- `SDL_Init failed: sdl2 not available`

原因：

- 设备 SDL2 的 video driver 名称并不是 `sdl2`

结论：

- 不要强行设置 `SDL_VIDEODRIVER=sdl2`，让 SDL 自动选择。

### 坑 3：Docker 安装 armhf SDL2 dev 包需要 multiarch

现象：

- `libsdl2-dev:armhf` 安装失败/找不到

解决：

- 在容器内执行：`dpkg --add-architecture armhf`

（在最终方案中，这一步已经固化到 builder 镜像中。）

### 坑 4：ImGui SDL2 backend API 版本差异

现象：

- `ImGui_ImplSDL2_NewFrame(window)` 编译失败

解决：

- 新版 ImGui 为 `ImGui_ImplSDL2_NewFrame()`（无参数）

### 坑 5：链接仓库自带 `libmali.so` 触发 ld 异常输出

现象：

- `ld: ./lib32/libmali.so: .dynsym local symbol ...`

解决：

- demo 链接阶段改用：`-lEGL -lGLESv2`
- 运行时通过 `LD_LIBRARY_PATH=/usr/lib32:...` 让设备选择 Mali 实现

### 坑 6：`tradeboy-ui-demo-armhf` 闪退（ImGui 初始化顺序错误）

现象：

- UI demo 启动后立即闪退
- 或者日志中只看到 SDL 初始化信息，后续没有任何渲染输出

原因：

- 在 `ImGui::CreateContext()` 之前调用了 `ImGui::GetIO()`（属于未定义行为，容易直接崩溃）

修复：

- 启动顺序必须是：
  - `SDL_Init`
  - `SDL_GL_SetAttribute(...)`
  - `SDL_CreateWindow(...)`
  - `SDL_GL_CreateContext(...)`
  - `SDL_GL_MakeCurrent(...)`
  - `ImGui::CreateContext()`
  - `ImGui::GetIO()` / 配置 flags / 加载字体
  - `ImGui_ImplSDL2_InitForOpenGL(...)`
  - `ImGui_ImplOpenGL3_Init("#version 100")`

补充要点（与主程序 `src/main.cpp` 保持一致）：

- 运行时 CWD：如果存在 `/mnt/mmc/Roms/APPS`，先 `chdir("/mnt/mmc/Roms/APPS")`，避免字体路径找不到
- Window 创建：优先使用 `SDL_WINDOWPOS_UNDEFINED_DISPLAY(0)` + `SDL_WINDOW_FULLSCREEN_DESKTOP`（与 RG34XX 的 mali-fbdev 更兼容）

## 清理掌机端测试文件

只清理 demo 相关文件（不影响系统菜单）：

```sh
ssh root@192.168.1.7 'rm -f /mnt/mmc/Roms/APPS/sdl2demo-armhf /mnt/mmc/Roms/APPS/sdl2demo-log.txt'
```
