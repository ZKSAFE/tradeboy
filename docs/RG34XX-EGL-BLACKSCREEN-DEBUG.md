# RG34XX：EGL 初始化成功但黑屏的排查与修复记录

## 目标与现象

目标：在 Anbernic RG34XX 上让 TradeBoy 的 GPU 渲染结果**真正显示到屏幕**。

现象：

- EGL / OpenGL ES 初始化成功（能拿到 `Mali-G31` 的 GL 字符串）。
- 主循环正常运行并输出 FPS。
- 但屏幕仍为黑屏，直到程序自动退出。

## 设备与关键环境

- 设备：RG34XX
- 内核：Linux 4.9.170（aarch64）
- framebuffer：`/dev/fb0`
- GPU 设备：`/dev/mali0`
- SSH：`root@192.168.1.7`（密码 `root`）

## 关键结论（最终有效的显示链路）

**结论：在该设备上，“EGL/GLES 渲染成功”并不等价于“画面可见”。**

经过验证，可靠的“可见”路径是：

- 使用 **SDL2 创建 window**
- 使用 SDL2 的 **OpenGL ES context**
- 用 `SDL_GL_SwapWindow()` 走平台的显示提交链路

而尝试绕过 SDL2、直接把 `glReadPixels()` 的结果写到 `/dev/fb0`，即使写入成功，也可能**不会进入最终的显示合成/扫描输出路径**，从而黑屏。

## 解决过程（按时间顺序）

### 1) EGL 初始化失败的根因：位宽/用户态栈不匹配

最开始的问题不是黑屏，而是 EGL 初始化失败（或初始化不稳定）。

观察系统上的可用库后发现：

- RG34XX 的 Mali EGL/GLES 用户态栈在 `/usr/lib32`，即 **32-bit armhf**。
- 64-bit aarch64 程序无法直接加载和使用这些 32-bit 库。

因此需要：

- 将 TradeBoy 构建为 **32-bit armhf**（`arm-linux-gnueabihf-gcc`）
- 运行时设置：
  - `LD_LIBRARY_PATH=/usr/lib32:/usr/lib:/mnt/vendor/lib`

结果：

- EGL / GLES 初始化成功
- 能稳定获取 `GL_RENDERER=Mali-G31`

### 2) “能跑但黑屏”：直接写 fb0 不可靠

此阶段 TradeBoy 已经：

- EGL/GLES 正常
- 渲染循环正常（FPS 正常）
- 尝试 `glReadPixels()` 把 RGBA 转 RGB565 写入 `/dev/fb0`

但屏幕仍黑。

辅助现象：`dmesg` 中出现与 display/fb 格式相关的报错（如 invalid fmt / bits_per_pixel 相关）。这些提示“写入了 fb”并不代表显示控制器/合成层会把它作为最终输出。

结论：黑屏的关键不在 EGL 初始化，而在“显示提交链路”缺失。

### 3) 验证显示链路：最小 SDL2 + GLES demo

为了验证“是不是必须用 SDL2 的那条路才能显示”，新增了一个最小 demo：

- 全屏窗口
- GLES2 shader
- 背景色每秒在 红/绿/蓝 切换
- 中间画一个方块
- `SDL_GL_SwapWindow()` 每帧提交

在设备上验证结果：

- **直接运行二进制**：画面可见（红/绿/蓝切换 + 方块）
- 证明：只要走 SDL2 window + swapbuffers，设备的显示系统能正常出画面

这一步非常关键：它把问题从“GPU/EGL 是否正常”转换成“TradeBoy 是否走了正确显示输出路径”。

## 踩过的坑（重要）

### 坑 1：以为 surfaceless EGL + 写 fb0 就能显示

在 PC/某些板子上，`/dev/fb0` 可能就是最终扫描输出；但 RG34XX 上更像是存在合成/图层/显示控制器配置，导致“写 fb0”不等价于“上屏”。

### 坑 2：脚本里强行设置 `SDL_VIDEODRIVER=sdl2`

在 RG34XX 上，SDL2 的 video driver 名称并不是 `sdl2`。

当脚本设置：

- `SDL_VIDEODRIVER=sdl2`

会直接导致：

- `SDL_Init failed: sdl2 not available`

而**直接运行二进制**时，SDL 会自动选择可用 driver，所以能正常显示。

因此：

- 不要在脚本里强行指定 `SDL_VIDEODRIVER=sdl2`
- 让 SDL 自动选择，或改为已验证的 driver 名称（需通过日志/环境确认）

### 坑 3：交叉编译容器内安装 `libsdl2-dev:armhf` 失败

在 Docker 的 Ubuntu ports 源偶发 502 时，会出现：

- `apt-get update` 报 502
- 或 `libsdl2-dev:armhf` 找不到

解决：

- 在容器内启用多架构：`dpkg --add-architecture armhf`
- 然后 `apt-get update && apt-get install libsdl2-dev:armhf`

## 当前状态与下一步

当前已经验证：

- SDL2 + GLES + swapwindow 可以可靠显示

下一步建议（迁移策略）：

- 将 TradeBoy 的渲染输出改为 SDL2 window + `SDL_GL_SwapWindow()`
- 避免 `glReadPixels -> 写 fb0`
- 继续保留现有 Mali/armhf 运行时库路径设置

## 清理测试文件（设备端）

仅删除本次测试用的 demo 与日志：

```sh
ssh root@192.168.1.7 'rm -f /mnt/mmc/Roms/APPS/sdl2demo-armhf /mnt/mmc/Roms/APPS/sdl2demo-start.sh /mnt/mmc/Roms/APPS/sdl2demo-log.txt /mnt/mmc/Roms/APPS/sdl2demo-start-trace.txt'
```

（注意：不要删除 `dmenu.bin` 等系统/菜单程序。）
