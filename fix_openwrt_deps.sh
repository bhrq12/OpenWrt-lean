#!/bin/bash

# 定义颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

# 检查是否在 OpenWrt 源码目录
if [ ! -f "feeds.conf.default" ]; then
    echo -e "${RED}错误：请在 OpenWrt 源码根目录下运行此脚本！${NC}"
    exit 1
fi

# 备份原始 feeds.conf.default
cp feeds.conf.default feeds.conf.default.bak
echo -e "${YELLOW}已备份 feeds.conf.default → feeds.conf.default.bak${NC}"

# 添加第三方软件源（kenzok8 和 xiaorouji）
echo -e "${GREEN}添加第三方软件源...${NC}"
if ! grep -q "kenzok8" feeds.conf.default; then
    echo "src-git kenzok8 https://github.com/kenzok8/openwrt-packages.git" >> feeds.conf.default
fi
if ! grep -q "passwall" feeds.conf.default; then
    echo "src-git passwall https://github.com/xiaorouji/openwrt-passwall.git" >> feeds.conf.default
fi
if ! grep -q "small" feeds.conf.default; then
    echo "src-git small https://github.com/kenzok8/small.git" >> feeds.conf.default
fi

# 更新 feeds 并安装
echo -e "${GREEN}更新 feeds 并安装依赖...${NC}"
./scripts/feeds update -a
./scripts/feeds install -a

# 检查并安装缺失的包
echo -e "${GREEN}检查缺失的包...${NC}"
MISSING_PKGS=()

# 1. luci-app-mosdns → v2dat
if [ ! -f "package/feeds/kenzok8/mosdns/v2dat" ]; then
    MISSING_PKGS+=("mosdns")
    echo -e "${YELLOW}警告：mosdns 的 v2dat 规则文件缺失，建议手动下载：${NC}"
    echo "  wget https://github.com/Loyalsoldier/v2ray-rules-dat/releases/latest/download/geoip.dat -O /etc/mosdns/geoip.dat"
    echo "  wget https://github.com/Loyalsoldier/v2ray-rules-dat/releases/latest/download/geosite.dat -O /etc/mosdns/geosite.dat"
fi

# 2. luci-app-passwall → chinadns-ng, dns2socks, tcping
for pkg in chinadns-ng dns2socks tcping; do
    if ! grep -q "$pkg" feeds.conf.default; then
        MISSING_PKGS+=("$pkg")
    fi
done

# 3. luci-app-vssr → pdnsd-alt, shadowsocks-libev-*, simple-obfs, trojan, hysteria
for pkg in pdnsd-alt shadowsocks-libev-ss-local shadowsocks-libev-ss-redir simple-obfs trojan hysteria; do
    if ! grep -q "$pkg" feeds.conf.default; then
        MISSING_PKGS+=("$pkg")
    fi
done

# 4. mqttled → python3-netifaces
if ! opkg list-installed | grep -q "python3-netifaces"; then
    MISSING_PKGS+=("python3-netifaces")
fi

# 提示用户手动安装缺失的包
if [ ${#MISSING_PKGS[@]} -gt 0 ]; then
    echo -e "${YELLOW}以下包可能缺失，请在 make menuconfig 中选中它们：${NC}"
    for pkg in "${MISSING_PKGS[@]}"; do
        echo "  - $pkg"
    done
    echo -e "${YELLOW}或运行：${NC}"
    echo "  make menuconfig"
    echo -e "${YELLOW}然后搜索并选中上述包。${NC}"
else
    echo -e "${GREEN}所有依赖已处理完毕！${NC}"
fi

echo -e "${GREEN}脚本执行完成！${NC}"
