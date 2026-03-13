#!/bin/bash

# TradeBoy上传到掌机脚本 v2.0
# 自动部署到RG34XX掌机 - 支持OpenGL ES
# 作者: Cascade (AI Assistant)
# 项目: TradeBoy - RG34XX交易机

set -e

# 默认配置
DEFAULT_IP="192.168.1.8"
DEFAULT_PASSWORD="root"
DEFAULT_USER="root"

# 解析命令行参数
IP=${1:-$DEFAULT_IP}
PASSWORD=${2:-$DEFAULT_PASSWORD}
USER=${3:-$DEFAULT_USER}

SSH_OPTS="-o StrictHostKeyChecking=no -o ConnectTimeout=10 -o PreferredAuthentications=password -o PubkeyAuthentication=no -o NumberOfPasswordPrompts=1"

retry() {
    local n=0
    local max=8
    local delay=1
    until "$@"; do
        n=$((n+1))
        if [ $n -ge $max ]; then
            return 1
        fi
        sleep $delay
        delay=$((delay*2))
        if [ $delay -gt 8 ]; then
            delay=8
        fi
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

if [ "$HAS_TRADEBOY" -eq 0 ]; then
    echo -e "${RED}❌ 错误: 当前目录没有 output/tradeboy-armhf${NC}"
    echo "请先编译:"
    echo "  - make tradeboy-armhf-docker"
    echo "  - make output-assets"
    exit 1
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
retry sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "mkdir -p /mnt/mmc/Roms/APPS/Imgs" 2>/dev/null

# Upload config and fonts (option)
echo "🧰 上传 tradeboy.cfg 和字体..."
if [ ! -f "tradeboy.cfg" ]; then
    echo -e "${RED}❌ tradeboy.cfg not found${NC}"
    exit 1
fi
if [ ! -f "cour-new.ttf" ]; then
    echo -e "${RED}❌ cour-new.ttf not found${NC}"
    exit 1
fi
if [ ! -f "cour-new-BOLDITALIC.ttf" ]; then
    echo -e "${RED}❌ cour-new-BOLDITALIC.ttf not found${NC}"
    exit 1
fi

retry sshpass -p "$PASSWORD" scp $SSH_OPTS "tradeboy.cfg" "$USER@$IP:/mnt/mmc/Roms/APPS/.tradeboy.cfg.tmp" || {
    echo -e "${RED}❌ 上传 tradeboy.cfg 失败${NC}"
    exit 1
}
retry sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "mv -f /mnt/mmc/Roms/APPS/.tradeboy.cfg.tmp /mnt/mmc/Roms/APPS/tradeboy.cfg" 2>/dev/null

retry sshpass -p "$PASSWORD" scp $SSH_OPTS "cour-new.ttf" "$USER@$IP:/mnt/mmc/Roms/APPS/.cour-new.ttf.tmp" || {
    echo -e "${RED}❌ 上传 cour-new.ttf 失败${NC}"
    exit 1
}
retry sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "mv -f /mnt/mmc/Roms/APPS/.cour-new.ttf.tmp /mnt/mmc/Roms/APPS/cour-new.ttf" 2>/dev/null

retry sshpass -p "$PASSWORD" scp $SSH_OPTS "cour-new-BOLDITALIC.ttf" "$USER@$IP:/mnt/mmc/Roms/APPS/.cour-new-BOLDITALIC.ttf.tmp" || {
    echo -e "${RED}❌ 上传 cour-new-BOLDITALIC.ttf 失败${NC}"
    exit 1
}
retry sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "mv -f /mnt/mmc/Roms/APPS/.cour-new-BOLDITALIC.ttf.tmp /mnt/mmc/Roms/APPS/cour-new-BOLDITALIC.ttf" 2>/dev/null

retry sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "chmod 644 /mnt/mmc/Roms/APPS/tradeboy.cfg /mnt/mmc/Roms/APPS/cour-new.ttf /mnt/mmc/Roms/APPS/cour-new-BOLDITALIC.ttf" 2>/dev/null || true


# Upload logo
if [ -f "logo.240x180.png" ]; then
    echo "🖼️  Uploading logo..."
    if ! retry sshpass -p "$PASSWORD" scp $SSH_OPTS "logo.240x180.png" "$USER@$IP:/mnt/mmc/Roms/APPS/Imgs/run-tradeboy-armhf.png"; then
        echo -e "${RED}❌ Upload logo failed${NC}"
        exit 1
    fi
    retry sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "chmod 644 /mnt/mmc/Roms/APPS/Imgs/run-tradeboy-armhf.png" 2>/dev/null || true
else
    echo -e "${YELLOW}⚠️  logo.240x180.png not found, skipping logo upload${NC}"
fi

# 上传文件
if [ "$HAS_TRADEBOY" -eq 1 ]; then
    echo "📤 上传TradeBoy可执行文件..."
    retry sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "rm -f /mnt/mmc/Roms/APPS/run-tradeboy-armhf.sh /mnt/mmc/Roms/APPS/tradeboy-armhf.bin /mnt/mmc/Roms/APPS/.tradeboy-armhf.tmp /mnt/mmc/Roms/APPS/.tradeboy-armhf.bin.tmp" 2>/dev/null || true
    if ! retry sshpass -p "$PASSWORD" scp $SSH_OPTS output/tradeboy-armhf "$USER@$IP:/mnt/mmc/Roms/APPS/.tradeboy-armhf.bin.tmp"; then
        echo -e "${RED}❌ 上传TradeBoy失败${NC}"
        exit 1
    fi
    retry sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "mv -f /mnt/mmc/Roms/APPS/.tradeboy-armhf.bin.tmp /mnt/mmc/Roms/APPS/tradeboy-armhf.bin" 2>/dev/null

    echo "🧩 Writing wrapper script (LD_LIBRARY_PATH)..."
    retry sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "cat > /mnt/mmc/Roms/APPS/run-tradeboy-armhf.sh <<'EOF'
#!/bin/sh
export LD_LIBRARY_PATH=/usr/lib32:/usr/lib:/mnt/vendor/lib
cd /mnt/mmc/Roms/APPS || exit 1
exec ./tradeboy-armhf.bin
EOF" 2>/dev/null
fi

# 设置文件权限
echo "🔧 设置文件权限..."
if [ "$HAS_TRADEBOY" -eq 1 ]; then
    retry sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "chmod 755 /mnt/mmc/Roms/APPS/run-tradeboy-armhf.sh /mnt/mmc/Roms/APPS/tradeboy-armhf.bin"
fi

# 验证安装结果
echo "✅ 验证安装结果..."
if [ "$HAS_TRADEBOY" -eq 1 ]; then
    retry sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "ls -lh /mnt/mmc/Roms/APPS/run-tradeboy-armhf.sh"
    retry sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "ls -lh /mnt/mmc/Roms/APPS/tradeboy-armhf.bin"
fi
retry sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "ls -lh /mnt/mmc/Roms/APPS/tradeboy.cfg /mnt/mmc/Roms/APPS/cour-new.ttf /mnt/mmc/Roms/APPS/cour-new-BOLDITALIC.ttf" 2>/dev/null || true

# 获取设备信息
echo ""
echo "📱 设备信息:"
DEVICE_INFO=$(sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "uname -a" 2>/dev/null || echo "无法获取")
echo -e "${BLUE}$DEVICE_INFO${NC}"

# 检查OpenGL ES支持
echo ""
echo "� 图形系统检查:"
GLES_INFO=$(sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "ls /dev/dri/ 2>/dev/null || echo '未找到DRM设备'" 2>/dev/null || echo "无法获取")
echo -e "${BLUE}$GLES_INFO${NC}"

FB_INFO=$(sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "ls -la /dev/fb* 2>/dev/null || echo '未找到帧缓冲设备'" 2>/dev/null || echo "无法获取")
echo -e "${BLUE}$FB_INFO${NC}"

# 完成提示
echo ""
echo -e "${GREEN}🎉 TradeBoy安装完成！${NC}"
echo "=================="
echo -e "运行命令:"
echo -e "${YELLOW}ssh $USER@$IP 'cd /mnt/mmc/Roms/APPS && ./run-tradeboy-armhf.sh'${NC}"
echo ""
echo -e "${GREEN}安装脚本执行完成！${NC}"

echo ""
echo -e "${YELLOW}Please start TradeBoy manually on the device (run run-tradeboy-armhf.sh from APPS).${NC}"
STARTED=""
if [ -t 0 ]; then
    read -r -p "你已经手动启动了吗？(y/N): " STARTED
else
    STARTED="N"
fi
if [[ "$STARTED" != "y" && "$STARTED" != "Y" ]]; then
    echo "未确认启动，脚本结束。你启动后可以重新运行本脚本并输入 y 以抓取日志。"
    exit 0
fi

echo ""
echo "📄 抓取最新 log.txt..."
if ! retry sshpass -p "$PASSWORD" ssh $SSH_OPTS "$USER@$IP" "tail -n 260 /mnt/mmc/Roms/APPS/log.txt"; then
    echo -e "${RED}❌ 读取 log.txt 失败（SSH可能偶发错误），请重试运行 upload.sh${NC}"
    exit 1
fi
