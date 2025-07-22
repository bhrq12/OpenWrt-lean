#!/bin/bash

# OpenWrt环境初始化脚本
set -e

# 基础依赖安装
sudo -E apt-get -qq update
sudo -E apt-get -qq install -y \
    ack antlr3 asciidoc autoconf automake autopoint \
    binutils bison build-essential bzip2 ccache \
    cmake cpio curl device-tree-compiler fastjar \
    flex gawk gettext gcc-multilib g++-multilib \
    git gperf haveged help2man intltool libc6-dev-i386 \
    libelf-dev libfuse-dev libglib2.0-dev libgmp3-dev \
    libltdl-dev libmpc-dev libmpfr-dev libncurses5-dev \
    libncursesw5-dev libpython3-dev libreadline-dev \
    libssl-dev libtool lrzsz mkisofs msmtp ninja-build \
    p7zip p7zip-full patch pkgconf python2.7 python3 \
    python3-pyelftools python3-setuptools qemu-utils \
    rsync scons squashfs-tools subversion swig \
    texinfo uglifyjs upx-ucl unzip vim wget xmlto \
    xxd zlib1g-dev python3-netifaces

# 额外编译工具
sudo -E apt-get -qq install -y clang g++ python3-netifaces

# 尝试安装网络工具（可能存在于特定源）
sudo -E apt-get -qq install -y dns2socks tcping pdnsd-alt trojan || {
    echo "警告: 部分网络工具安装失败，可能需要手动处理"
}

# 自定义包安装示例
if [ ! -d "custom-packages" ]; then
    echo "正在克隆自定义包仓库..."
    git clone --depth=1 https://github.com/example/custom-packages.git || {
        echo "警告: 自定义包仓库克隆失败"
        exit 0
    }
    pushd custom-packages
    make -j$(nproc) && sudo make install || {
        echo "警告: 自定义包编译安装失败"
    }
    popd
fi

# 清理工作
sudo -E apt-get -qq autoremove --purge
sudo -E apt-get -qq clean

echo "OpenWrt依赖安装完成"
