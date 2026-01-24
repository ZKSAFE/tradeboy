# TradeBoy 架构文档 2.0

> 版本: 2.0  
> 更新日期: 2025-01-24  
> 目标设备: RG34XX (armhf, 32-bit)

## 概述

TradeBoy 是一个运行在 RG34XX 掌机上的加密货币交易应用。本文档描述了 2.0 版本的架构改进和关键设计决策。

## 架构改进 (1.0 → 2.0)

### 新增模块

| 模块 | 文件 | 说明 |
|------|------|------|
| **Logger** | `src/core/Logger.{h,cpp}` | 单例日志系统，避免频繁文件开关 |
| **WebSocketClient** | `src/core/WebSocketClient.{h,cpp}` | 独立的 WebSocket 协议实现 |
| **DialogState** | `src/ui/DialogState.h` | 统一的对话框状态管理 |
| **picojson** | `third_party/picojson/picojson.h` | 轻量级 JSON 解析库 |

### 关键改进

1. **统一 mutex 类型**: 全部使用 `pthread_mutex_t` 替代 `std::mutex`
2. **DialogState 重构**: 消除重复的对话框状态变量 (~21行 → 3行)
3. **日志系统优化**: 单例模式，每次写入后立即 flush
4. **代码注释清理**: 移除过多的调试日志，添加架构保护注释

---

## 目录结构

```
src/
├── core/                    # 核心基础设施
│   ├── Logger.{h,cpp}       # 日志系统 (单例)
│   └── WebSocketClient.{h,cpp}  # WebSocket 客户端
├── app/                     # 应用层
│   ├── App.{h,cpp}          # 主应用逻辑
│   └── Input.{h,cpp}        # 输入处理
├── model/                   # 数据模型
│   └── TradeModel.{h,cpp}   # 中央状态管理 ⚠️ CRITICAL
├── market/                  # 市场数据
│   ├── IMarketDataSource.h  # 数据源接口
│   ├── HyperliquidWsDataSource.{h,cpp}  # WebSocket 数据源 ⚠️ CRITICAL
│   ├── MarketDataService.{h,cpp}  # 数据服务
│   └── Hyperliquid.{h,cpp}  # API 解析
├── arb/                     # Arbitrum 链上交互
│   ├── ArbitrumRpc.{h,cpp}  # RPC 调用
│   └── ArbitrumRpcService.{h,cpp}  # RPC 服务
├── ui/                      # UI 组件
│   ├── DialogState.h        # 对话框状态
│   ├── Dialog.{h,cpp}       # 对话框渲染
│   └── MainUI.{h,cpp}       # 主 UI
├── spot/                    # Spot 交易界面
├── perp/                    # Perp 交易界面
├── account/                 # 账户界面
├── wallet/                  # 钱包管理
├── filters/                 # 视觉滤镜
└── utils/                   # 工具函数

third_party/
├── imgui/                   # ImGui 库
└── picojson/                # JSON 解析库
```

---

## 核心架构

### 数据流

```
┌─────────────────────────────────────────────────────────────────┐
│                         UI Layer (Read-Only)                     │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐        │
│  │SpotScreen│  │PerpScreen│  │AccountScr│  │  Dialog  │        │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘        │
│       │             │             │             │                │
│       └─────────────┴─────────────┴─────────────┘                │
│                           │                                      │
│                    snapshot() 读取                               │
│                           ▼                                      │
├─────────────────────────────────────────────────────────────────┤
│                      TradeModel (Mutex Protected)                │
│                    ⚠️ 使用 pthread_mutex_t                       │
├─────────────────────────────────────────────────────────────────┤
│                           ▲                                      │
│                    set_xxx() 写入                                │
│                           │                                      │
│  ┌────────────────────────┴────────────────────────┐            │
│  │                  Data Layer                      │            │
│  │  ┌─────────────────┐  ┌─────────────────────┐   │            │
│  │  │MarketDataService│  │ArbitrumRpcService   │   │            │
│  │  │  (后台线程)      │  │  (后台线程)          │   │            │
│  │  └────────┬────────┘  └──────────┬──────────┘   │            │
│  │           │                      │              │            │
│  │           ▼                      ▼              │            │
│  │  ┌─────────────────┐  ┌─────────────────────┐   │            │
│  │  │HyperliquidWsData│  │   ArbitrumRpc       │   │            │
│  │  │Source (WS线程)  │  │   (HTTP/wget)       │   │            │
│  │  └─────────────────┘  └─────────────────────┘   │            │
│  └──────────────────────────────────────────────────┘            │
└─────────────────────────────────────────────────────────────────┘
```

### 线程模型

| 线程 | 职责 | 关键约束 |
|------|------|----------|
| **Main Thread** | SDL 事件循环, ImGui 渲染 | 只读 TradeModel |
| **MarketDataService** | 定期获取市场数据 | 写入 TradeModel |
| **ArbitrumRpcService** | 定期获取链上数据 | 写入 TradeModel |
| **HyperliquidWsDataSource** | WebSocket 连接管理 | 内部缓存数据 |

---

## 关键模块说明

### TradeModel ⚠️ CRITICAL

**文件**: `src/model/TradeModel.{h,cpp}`

中央状态管理，所有线程共享。

```cpp
// ⚠️ 必须使用 pthread_mutex_t，不要用 std::mutex
mutable pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;

// ⚠️ std::vector 赋值必须用 swap()，不要用 move-assignment
void set_spot_rows(std::vector<SpotRow> rows) {
    pthread_mutex_lock(&mu);
    spot_rows_.swap(rows);  // 不要用 spot_rows_ = std::move(rows);
    pthread_mutex_unlock(&mu);
}
```

**设计模式**: Snapshot Pattern
- UI 通过 `snapshot()` 获取只读快照
- 数据层通过 `set_xxx()` 写入

### HyperliquidWsDataSource ⚠️ CRITICAL

**文件**: `src/market/HyperliquidWsDataSource.{h,cpp}`

WebSocket 数据源，通过 `openssl s_client` 子进程实现。

**关键点**:
- 不使用 libwebsockets 库，避免依赖问题
- 手动实现 WebSocket 帧解析
- 后台线程管理连接和重连
- 数据缓存在内存中，MarketDataService 定期读取

### Logger

**文件**: `src/core/Logger.{h,cpp}`

单例日志系统。

```cpp
// 使用方式
log_str("[Module] message\n");

// ⚠️ 禁止使用 varargs 日志 (会导致 RG34XX 崩溃)
// log_to_file("format %s", arg);  // 禁止!
```

### DialogState

**文件**: `src/ui/DialogState.h`

统一的对话框状态管理。

```cpp
struct DialogState {
    bool open = false;
    bool closing = false;
    int open_frames = 0;
    int close_frames = 0;
    int flash_frames = 0;
    int selected_btn = 0;
    int pending_action = -1;
    std::string body;
    
    void open_dialog(const std::string& body, int default_btn);
    void start_flash(int action);
    void start_close();
    bool tick_flash();
    bool tick_close_anim();
    void tick_open_anim();
    float get_open_t() const;
    void reset();
};
```

---

## 设备约束 (RG34XX)

### 必须遵守

| 约束 | 原因 | 解决方案 |
|------|------|----------|
| **禁用 std::mutex** | ABI 不兼容，导致崩溃 | 使用 `pthread_mutex_t` |
| **禁用 varargs 日志** | 栈损坏，导致崩溃 | 使用 `log_str(const char*)` |
| **禁用 std::vector move** | SIGSEGV | 使用 `swap()` |
| **32-bit armhf** | 设备架构 | Docker 交叉编译 |

### 编译命令

```bash
./build.sh  # 调用 make tradeboy-armhf-docker
```

### 部署命令

```bash
./upload.sh [IP] [PASSWORD]
```

---

## 模块调用关系

```
main.cpp
    │
    ├── Logger::init()           # 初始化日志
    ├── SDL_Init()               # 初始化 SDL
    ├── App::startup()           # 启动应用
    │       │
    │       ├── load_wallet()    # 加载钱包
    │       ├── MarketDataService::start()
    │       │       └── HyperliquidWsDataSource (内部线程)
    │       └── ArbitrumRpcService::start()
    │
    ├── Main Loop
    │       ├── App::handle_input()
    │       └── App::render()
    │               ├── render_spot_screen()
    │               ├── render_perp_screen()
    │               ├── render_account_screen()
    │               └── render_dialog()
    │
    ├── App::shutdown()
    │       ├── MarketDataService::stop()
    │       └── ArbitrumRpcService::stop()
    │
    └── Logger::shutdown()
```

---

## 代码修改指南

### ⚠️ 禁止修改的代码

1. **TradeModel 的 mutex 类型** - 必须是 `pthread_mutex_t`
2. **std::vector 的赋值方式** - 必须用 `swap()`
3. **日志函数签名** - 必须是 `log_str(const char*)`
4. **WebSocket 帧解析逻辑** - 经过大量调试，非常脆弱

### ⚠️ 谨慎修改的代码

1. **JSON 解析函数** (`MarketDataService.cpp`) - 手写解析，已稳定
2. **HyperliquidWsDataSource::run()** - 复杂的状态机
3. **Dialog 动画逻辑** - 帧计数敏感

### ✅ 可以安全修改的代码

1. **UI 布局和样式** - `*Screen.cpp` 文件
2. **新增功能** - 遵循现有模式
3. **配置参数** - 超时、间隔等

---

## 常见问题

### Q: 为什么不用 std::mutex?

A: RG34XX 的 armhf 工具链与 libstdc++ 的 std::mutex 实现有 ABI 兼容问题，会导致随机崩溃。pthread_mutex_t 是 C 库的一部分，更稳定。

### Q: 为什么用 openssl s_client 而不是 libwebsockets?

A: 减少依赖，避免交叉编译问题。openssl 在设备上已经存在。

### Q: 为什么 JSON 解析是手写的?

A: 最初是为了避免依赖。现在已引入 picojson，新代码可以使用它，但旧代码已稳定，不建议迁移。

### Q: 日志文件在哪里?

A: `/mnt/mmc/Roms/APPS/log.txt` (设备上) 或 `./log.txt` (本地)

---

## 版本历史

| 版本 | 日期 | 主要变更 |
|------|------|----------|
| 1.0 | 2025-01 | 初始架构 |
| 2.0 | 2025-01-24 | Logger 重构, DialogState 统一, pthread_mutex_t 统一, picojson 引入 |
