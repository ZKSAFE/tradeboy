# TradeBoy - RG34XX 掌机交易平台

## 📋 项目概述

TradeBoy 是专为 RG34XX 掌机设计的商品交易平台，使用 SDL2 + OpenGL ES 开发，支持即时模式 UI 和多语言显示。

### 🎮 功能特性

- 🛒 **商品交易平台** - 5种商品展示和选择
- 🎮 **手柄操作支持** - 完整的按键映射
- 🌏 **多语言支持** - UTF-8 + CJK字体
- ⏰ **自动退出机制** - 15秒无操作休眠
- 🎨 **OpenGL ES渲染** - 针对嵌入式设备优化
- 📱 **即时模式UI** - 现代化界面组件

## 📁 项目结构

```
tradeboy/
├── src/
│   └── tradeboy-main.c          # 主程序源码
├── Makefile                     # 编译配置
├── install.sh                   # 自动安装脚本
├── NotoSansCJK-Regular.ttc      # 中文字体文件
├── tradeboy-arm                 # ARM64可执行文件
└── PROJECT-DOCUMENTATION.md     # 项目文档
```

## 🔧 依赖环境

### 开发环境
- **操作系统**: macOS/Linux
- **编译器**: GCC
- **Docker**: 用于交叉编译

### 运行时依赖
- **SDL2**: 图形和输入处理
- **SDL2_ttf**: 字体渲染
- **OpenGL ES**: 图形渲染
- **Noto Sans CJK**: 中文字体支持

### 掌机环境
- **设备**: Anbernic RG34XX
- **系统**: Linux 4.9.170 (aarch64)
- **图形**: 帧缓冲 /dev/fb0
- **输入**: SDL2手柄支持

## 🏗️ 编译方式

### 方法1: Docker交叉编译 (推荐)

```bash
# 使用rg34xx-sdl2-builder镜像编译
docker run --rm \
  -v "$(pwd):/workspace" \
  rg34xx-sdl2-builder:latest \
  sh -c "
    cd /workspace
    make clean
    make tradeboy-arm CC=aarch64-linux-gnu-gcc
  "
```

### 方法2: 本地编译 (Mac)

```bash
make clean
make sdl2-mac
```

### 方法3: 直接使用预编译文件

项目已包含预编译的 `tradeboy-arm` 文件，可直接用于部署。

## 📦 部署安装

### 自动安装

```bash
# 使用默认配置 (IP: 192.168.3.97)
./install.sh

# 指定IP和密码
./install.sh 192.168.3.97 root
```

### 手动安装

```bash
# 上传文件
scp tradeboy-arm root@192.168.3.97:/mnt/mmc/Roms/APPS/
scp NotoSansCJK-Regular.ttc root@192.168.3.97:/mnt/mmc/Roms/APPS/

# 设置权限
ssh root@192.168.3.97 "chmod 755 /mnt/mmc/Roms/APPS/tradeboy-arm"

# 运行
ssh root@192.168.3.97 "cd /mnt/mmc/Roms/APPS && ./tradeboy-arm"
```

## 🐛 踩过的坑

### 1. SDL2窗口创建失败

**问题**: `Could not initialize EGL`

**原因**: 
- SDL2默认尝试初始化桌面版OpenGL
- Anbernic设备只支持OpenGL ES
- EGL驱动不兼容

**解决方案**:
```c
// 强制使用OpenGL ES
SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
```

### 2. SDL2兼容性问题

**问题**: `SDL_HINT_VIDEO_DRIVER` 未定义

**原因**: 旧版SDL2不支持该hint

**解决方案**:
```c
#ifdef __linux__
// SDL_HINT_VIDEO_DRIVER 可能在旧版SDL2中不存在
// SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "gles2");
#endif
```

### 3. 主循环不执行

**问题**: `Total frames: 0`

**原因**: `tradeboy_context_t app = {0};` 将 `app.running` 初始化为0

**解决方案**:
```c
tradeboy_context_t app = {0};
app.running = 1;  // 显式设置为运行状态
```

### 4. 帧缓冲检测失败

**问题**: 交叉编译时跳过帧缓冲检测

**原因**: `#ifdef __CROSS_COMPILE__` 条件限制

**解决方案**: 移除条件限制，在Linux环境下总是检查帧缓冲

### 5. 字体渲染问题

**问题**: 掌机缺少中文字体

**解决方案**: 
- 使用Noto Sans CJK字体
- 多路径字体加载策略
- 备选系统字体支持

### 6. Docker网络问题

**问题**: Docker Hub连接超时

**解决方案**: 
- 使用本地缓存镜像
- 使用rg34xx-sdl2-builder镜像
- 离线构建策略

## 🎯 技术架构

### 图形系统
- **SDL2**: 跨平台多媒体库
- **OpenGL ES 2.0**: 嵌入式图形API
- **帧缓冲**: 直接硬件访问

### UI组件
- **UIButton**: 按钮组件 (hover状态)
- **UIListBox**: 列表框组件 (选中高亮)
- **UIDialog**: 对话框组件 (半透明背景)

### 输入处理
- **键盘**: 方向键、Enter、ESC
- **手柄**: ANBERNIC按键映射
- **自动退出**: 15秒无操作

## 📊 性能优化

### 内存管理
- 纹理创建和销毁
- 字体缓存机制
- 双缓冲渲染

### 渲染优化
- 软件渲染备选方案
- 帧率控制 (30FPS)
- 区域更新策略

## 🔍 调试技巧

### 日志系统
```c
void log_message(const char* message) {
    FILE* log_file = fopen("/tmp/tradeboy.log", "a");
    if (log_file) {
        fprintf(log_file, "[%ld] %s\n", time(NULL), message);
        fclose(log_file);
    }
}
```

### 调试命令
```bash
# 查看日志
ssh root@192.168.3.97 "cat /tmp/tradeboy.log"

# 检查图形设备
ssh root@192.168.3.97 "ls -la /dev/fb* /dev/dri/*"

# 检查SDL2版本
ssh root@192.168.3.97 "sdl2-config --version"
```

## 🚀 未来改进

### 短期目标
- [ ] 解决EGL初始化问题
- [ ] 添加更多UI组件
- [ ] 优化触摸屏支持

### 长期目标
- [ ] 网络功能集成
- [ ] 商品数据库
- [ ] 用户界面主题

## 📝 开发笔记

### OpenGL ES vs OpenGL
- OpenGL ES是嵌入式设备的简化版本
- 移除了许多桌面版OpenGL功能
- 需要特殊的着色器和纹理处理

### 帧缓冲 vs DRM
- 帧缓冲是传统的Linux图形接口
- DRM是现代的内核图形子系统
- Anbernic设备主要使用帧缓冲

### 交叉编译注意事项
- 目标平台的库版本必须匹配
- 静态链接可减少依赖问题
- 调试信息需要特殊处理

## 📞 支持

如有问题，请检查：
1. 设备网络连接
2. SSH服务状态
3. 字体文件完整性
4. SDL2库版本兼容性

---

**项目版本**: v2.0  
**最后更新**: 2025年1月7日  
**作者**: Cascade (AI Assistant)
