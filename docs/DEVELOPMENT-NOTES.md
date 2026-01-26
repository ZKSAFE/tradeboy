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

### 坑：掌机 SDL2 缺少 `SDL_OpenURL` 导致启动闪退（undefined symbol）

现象：

- 在掌机上运行时立即退出。
- 通过 SSH 直接运行二进制可看到：`undefined symbol: SDL_OpenURL`。

原因：

- 编译时使用的 SDL2 headers/版本较新（例如 ImGui 的 `imgui_impl_sdl2.cpp` 会调用 `SDL_OpenURL`）。
- 掌机运行时的 SDL2 库较旧，不包含该符号，动态链接失败。

修复：

- 在可执行文件里提供一个兼容 stub（旧 SDL2 上返回失败即可），保证动态链接阶段符号可解析。
- TradeBoy 采用的修复方式：在 `src/main.cpp` 提供：
  - `extern "C" int SDL_OpenURL(const char* url) { (void)url; return -1; }`

### 坑 7：RG34XX 启动期 SIGSEGV（`std::mutex` / 变参日志 / `std::string`）

现象：

- 掌机上启动后在初始化阶段随机闪退（`SIGSEGV`），常出现在 `App::init_demo_data()` / `TradeModel::set_spot_rows()` 附近
- backtrace 很浅，偶尔指向 `std::string` 赋值/析构，或者直接落在上层函数地址（典型“栈/返回地址被破坏”）

根因（结论）：

- RG34XX 的 armhf 用户态环境下，`std::mutex`/libstdc++/glibc + 变参 `vfprintf` 的组合存在 ABI/实现差异导致的不稳定（容易触发未定义行为/栈破坏）。
- 这类问题的特征是：看起来像业务代码某行崩了，但实际上是更早的 ABI/变参路径把内存/栈破坏了。

定位方法（建议流程）：

- 在关键路径打“分段日志”确认崩溃点是否发生在某函数返回之前/之后。
- 优先把日志改为“常量字符串写入”验证是否为 varargs/ABI：
  - 新增 `log_str(const char*)`，内部用 `fputs` 追加到 `log.txt`（避免 `va_list/vfprintf`）。
- 一旦怀疑死锁/锁相关：可以临时跳过 `unlock` 验证是否是 `unlock` 触发崩溃，但注意这会导致后续必然卡死（用于诊断即可）。

稳定修复（最终采用）：

- **锁替换**：`TradeModel` 的 `std::mutex` 替换为 `pthread_mutex_t`，并在构造/析构里 `pthread_mutex_init/destroy`。
- **降低 string 赋值链路风险**：`set_spot_rows` 使用 `spot_rows_.swap(rows)`，避免大量 `std::string` 赋值/拷贝。
- **关键日志改为非变参**：启动期关键点（model ctor / set_spot_rows / set_spot_row_idx / init_demo_data return）使用 `log_str`。

验证点（健康日志应出现）：

- `[Model] ctor`
- `set_spot_rows unlocked`
- `[App] init_demo_data returned set_spot_rows`
- `[Main] entering main loop`

常见误判：

- 看到卡在 `set_spot_row_idx about to lock`：通常是之前为诊断跳过了 `unlock` 导致死锁，并非性能问题。

## 清理掌机端测试文件

只清理 demo 相关文件（不影响系统菜单）：

```sh
ssh root@192.168.1.7 'rm -f /mnt/mmc/Roms/APPS/sdl2demo-armhf /mnt/mmc/Roms/APPS/sdl2demo-log.txt'
```

## 坑：改了 `TradeModel.h` / `App` 布局后，增量编译未重编导致掌机随机崩溃（ABI/对象大小不一致）

现象：

- 修改 `TradeModel.h`（例如给 `AccountSnapshot`/`TradeModel` 增加字段）后，**本地编译可能“看起来成功”**，但上传到掌机会出现启动阶段 `SIGSEGV` 或渲染阶段 `SIGABRT`。
- backtrace 往往很浅，地址落在 `App::startup()` / `App::render()` 附近，像是业务代码崩了。

根因（结论）：

- 当前 Makefile 依赖跟踪不完整：改动头文件后，某些 TU（如 `src/main.cpp` / `src/app/App.cpp`）可能**没有被重新编译**。
- 在 RG34XX 上这会变成“硬崩溃”：
  - 旧的 `main.o` 可能按旧的 `sizeof(App)` 去 `new App`，分配内存偏小，随后 `startup()` 写成员时越界 -> `SIGSEGV`。
  - 或者旧的调用方按旧的 `AccountSnapshot` 布局读写，产生未定义行为 -> `SIGABRT`。

解决办法：

- **推荐**：做一次全量构建（例如清理 `build/armhf` 或等价 clean），确保所有依赖头文件的目标重新编译。
- **诊断/临时 workaround**：在疑似没被重编的 TU 里加一个 no-op 改动（例如在 `main()` 或 `App::render()` 顶部加一个 `static const int GUARD=1; (void)GUARD;`），强制该文件重新编译。
- 验证：重新上传后如果崩溃消失，基本可确认是“增量编译导致 ABI/布局不一致”。
