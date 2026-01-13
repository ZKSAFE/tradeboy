#!/bin/bash

# TradeBoy安装脚本 v2.0
# 自动部署到RG34XX掌机 - 支持OpenGL ES
# 作者: Cascade (AI Assistant)
# 项目: TradeBoy - RG34XX交易平台

set -e

# 默认配置
DEFAULT_IP="192.168.66.194"
DEFAULT_PASSWORD="root"
DEFAULT_USER="root"

# 解析命令行参数
IP=${1:-$DEFAULT_IP}
PASSWORD=${2:-$DEFAULT_PASSWORD}
USER=${3:-$DEFAULT_USER}

SSH_OPTS="-o StrictHostKeyChecking=no -o ConnectTimeout=10 -o PreferredAuthentications=password -o PubkeyAuthentication=no -o NumberOfPasswordPrompts=1"

retry() {
    local n=0
    local max=3
    local delay=1
    until "$@"; do
        n=$((n+1))
        if [ $n -ge $max ]; then
            return 1
        fi
        sleep $delay
        delay=$((delay*2))
    done
}

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}🛒 TradeBoy 安装脚本 v2.0${NC}"
echo "=================="
echo -e "目标设备: ${GREEN}$IP${NC}"
echo -e "用户: ${GREEN}$USER${NC}"
echo -e "密码: ${YELLOW}[隐藏]${NC}"
echo ""

# 检查必要文件
echo "📋 检查必要文件..."
HAS_TRADEBOY=0

if [ -f "output/tradeboy-armhf" ]; then
    HAS_TRADEBOY=1
fi

HAS_DEMO=0
if [ -f "output/tradeboy-ui-demo-armhf" ]; then
    HAS_DEMO=1
fi

if [ "$HAS_TRADEBOY" -eq 0 ]; then
    echo -e "${RED}❌ 错误: 当前目录没有 output/tradeboy-armhf${NC}"
    echo "请先编译:"
    echo "  - make tradeboy-armhf-docker"
    echo "  - make output-assets"
    exit 1
fi

HAS_FONT=0
if [ -f "output/NotoSansCJK-Regular.ttc" ]; then
    HAS_FONT=1
fi

echo -e "${GREEN}✅ 文件检查完成${NC}"



# 测试SSH连接
echo "🔗 测试SSH连接..."
if ! retry sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "echo 'SSH连接成功'" 2>/dev/null; then
    echo -e "${RED}❌ SSH连接失败${NC}"
    echo "请检查:"
    echo "  - IP地址是否正确: $IP"
    echo "  - 掌机是否开机"
    echo "  - SSH服务是否启用"
    echo "  - 密码是否正确"
    exit 1
fi

echo -e "${GREEN}✅ SSH连接成功${NC}"

# 创建应用目录
echo "📁 创建应用目录..."
retry sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "mkdir -p /mnt/mmc/Roms/APPS" 2>/dev/null

# 上传文件
if [ "$HAS_TRADEBOY" -eq 1 ]; then
    echo "📤 上传TradeBoy可执行文件..."
    retry sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "rm -f /mnt/mmc/Roms/APPS/tradeboy-armhf /mnt/mmc/Roms/APPS/.tradeboy-armhf.tmp" 2>/dev/null || true
    if ! retry sshpass -p "$PASSWORD" scp $SSH_OPTS output/tradeboy-armhf "$USER@$IP:/mnt/mmc/Roms/APPS/.tradeboy-armhf.tmp"; then
        echo -e "${RED}❌ 上传TradeBoy失败${NC}"
        exit 1
    fi
    retry sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "mv -f /mnt/mmc/Roms/APPS/.tradeboy-armhf.tmp /mnt/mmc/Roms/APPS/tradeboy-armhf" 2>/dev/null
fi

if [ "$HAS_DEMO" -eq 1 ]; then
    echo "📤 上传UI Demo (tradeboy-ui-demo-armhf)..."
    retry sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "rm -f /mnt/mmc/Roms/APPS/tradeboy-ui-demo-armhf /mnt/mmc/Roms/APPS/.tradeboy-ui-demo-armhf.tmp" 2>/dev/null || true
    if ! retry sshpass -p "$PASSWORD" scp $SSH_OPTS output/tradeboy-ui-demo-armhf "$USER@$IP:/mnt/mmc/Roms/APPS/.tradeboy-ui-demo-armhf.tmp"; then
        echo -e "${RED}❌ 上传UI Demo失败${NC}"
        exit 1
    fi
    retry sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "mv -f /mnt/mmc/Roms/APPS/.tradeboy-ui-demo-armhf.tmp /mnt/mmc/Roms/APPS/tradeboy-ui-demo-armhf" 2>/dev/null
fi

if [ "$HAS_FONT" -eq 1 ]; then
    echo "📤 上传字体文件..."
    if ! retry sshpass -p "$PASSWORD" scp $SSH_OPTS output/NotoSansCJK-Regular.ttc "$USER@$IP:/mnt/mmc/Roms/APPS/"; then
        echo -e "${RED}❌ 上传字体文件失败${NC}"
        exit 1
    fi
fi

# 设置文件权限
echo "🔧 设置文件权限..."
if [ "$HAS_TRADEBOY" -eq 1 ]; then
    retry sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "chmod 755 /mnt/mmc/Roms/APPS/tradeboy-armhf"
fi
if [ "$HAS_DEMO" -eq 1 ]; then
    retry sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "chmod 755 /mnt/mmc/Roms/APPS/tradeboy-ui-demo-armhf"
fi
if [ "$HAS_FONT" -eq 1 ]; then
    retry sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "chmod 644 /mnt/mmc/Roms/APPS/NotoSansCJK-Regular.ttc"
fi

# 验证安装结果
echo "✅ 验证安装结果..."
if [ "$HAS_TRADEBOY" -eq 1 ]; then
    retry sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "ls -lh /mnt/mmc/Roms/APPS/tradeboy-armhf"
fi
if [ "$HAS_DEMO" -eq 1 ]; then
    retry sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "ls -lh /mnt/mmc/Roms/APPS/tradeboy-ui-demo-armhf"
fi
if [ "$HAS_FONT" -eq 1 ]; then
    retry sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "ls -lh /mnt/mmc/Roms/APPS/NotoSansCJK-Regular.ttc"
fi

# 获取设备信息
echo ""
echo "📱 设备信息:"
DEVICE_INFO=$(sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "uname -a" 2>/dev/null || echo "无法获取")
echo -e "${BLUE}$DEVICE_INFO${NC}"

# 检查OpenGL ES支持
echo ""
echo "� 图形系统检查:"
GLES_INFO=$(sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "ls /dev/dri/ 2>/dev/null || echo '未找到DRM设备'")
echo -e "${BLUE}$GLES_INFO${NC}"

FB_INFO=$(sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "ls -la /dev/fb* 2>/dev/null || echo '未找到帧缓冲设备'")
echo -e "${BLUE}$FB_INFO${NC}"

# 完成提示
echo ""
echo -e "${GREEN}�🎉 TradeBoy安装完成！${NC}"
echo "=================="
echo -e "运行命令:"
echo -e "${YELLOW}ssh $USER@$IP 'cd /mnt/mmc/Roms/APPS && ./tradeboy-armhf'${NC}"
echo ""
echo -e "${GREEN}安装脚本执行完成！${NC}"
