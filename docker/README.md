# RK3588交叉编译Docker环境

## 🎯 概述

本项目提供专为RK3588设计的交叉编译Docker环境，使用Ubuntu 22.04作为基础镜像，预装aarch64-linux-gnu交叉编译工具链和RK3588专用库。

## 🔧 环境要求

- Docker Engine 20.10+
- Docker Compose
- Linux系统（推荐Ubuntu 20.04+）
- SSH访问RK3588设备（通过mDNS自动发现）

## 🚀 快速开始

### 1. 启动交叉编译环境

```bash
cd docker
chmod +x start.sh build.sh
./start.sh
```

### 2. 进入交叉编译容器

```bash
docker compose exec gstreamer-rknn-cross bash
```

### 3. 执行交叉编译

```bash
# 仅编译
./docker/build.sh build

# 编译并部署到RK3588
./docker/build.sh all

# 仅部署
./docker/build.sh deploy
```

## 📁 目录结构

```
docker/
├── Dockerfile                 # RK3588交叉编译专用镜像
├── docker-compose.yml        # Docker Compose配置
├── start.sh                 # 一键启动脚本
├── build.sh                 # 交叉编译+部署脚本
├── test_workflow.sh         # 端到端工作流测试脚本
├── meson-cross-aarch64.txt  # Meson交叉编译配置
├── .env.example             # 环境变量模板
├── .env                     # 自动生成的环境变量文件
├── .root/                   # 容器root用户主目录（持久化）
└── .home/                   # 容器当前用户主目录（持久化）
```

## 🛠️ 技术规格

### 交叉编译工具链
- **目标架构**: aarch64-linux-gnu
- **GCC版本**: 11.x
- **工具链**: Ubuntu官方aarch64-linux-gnu
- **构建系统**: Meson + Ninja

### RK3588专用组件
- **RGA库**: librga.so (Rockchip Graphics Acceleration)
- **RKNN Runtime**: librknnrt.so
- **GStreamer**: 1.20+ (aarch64版本)

## 📋 使用方法

### 基础操作

```bash
# 启动环境
./start.sh

# 进入容器
docker compose exec gstreamer-rknn-cross bash

# 在容器内执行
cd /workspace

# 配置构建
meson setup build-aarch64 --cross-file docker/meson-cross-aarch64.txt

# 编译
ninja -C build-aarch64

# 安装到容器
ninja -C build-aarch64 install
```

### 一键操作

```bash
# 完整流程：清理+构建+部署
./docker/build.sh all

# 仅构建
./docker/build.sh build

# 仅部署到设备
./docker/build.sh deploy

# 清理构建
./docker/build.sh clean
```

## 🔧 配置说明

### 环境变量 (.env文件)

```bash
# 用户配置
USER_ID=$(id -u)
GROUP_ID=$(id -g)
USER_NAME=$(whoami)

# 目标设备（通过mDNS自动发现）
TARGET_DEVICE=opi-003.local
SSH_USER=root
```

### 目标设备配置

目标RK3588设备通过mDNS自动发现，默认地址为`opi-003.local`。确保：
1. 设备已启用SSH服务
2. 主机和目标设备在同一网络
3. mDNS正常工作（可以ping opi-003.local）

## 🐛 故障排除

### 1. mDNS解析失败
```bash
# 测试mDNS解析
ping opi-003.local

# 如果失败，检查：
# - 设备是否已启动
# - 网络连接是否正常
# - 容器内mDNS支持（已预装avahi-utils）
# - 使用IP地址替代
```

### 2. SSH连接失败
```bash
# 测试SSH连接
ssh root@opi-003.local

# 如果需要密码，设置SSH密钥
ssh-copy-id root@opi-003.local
```

### 3. 交叉编译错误
```bash
# 检查交叉编译器
aarch64-linux-gnu-gcc --version

# 检查库文件
pkg-config --cflags --libs gstreamer-1.0
```

### 4. 构建缓存问题
```bash
# 清理并重新构建
./docker/build.sh clean
./docker/build.sh build
```

## 📊 验证测试

### 验证交叉编译结果

```bash
# 在容器中检查架构
file build-aarch64/src/libgstrknn.so
# 输出应包含: ELF 64-bit LSB shared object, ARM aarch64

# 验证依赖
ldd build-aarch64/src/libgstrknn.so
```

### 端到端工作流测试

使用提供的测试脚本验证从交叉编译到部署的完整流程：

```bash
# 运行端到端测试
./docker/test_workflow.sh

# 手动测试插件
gst-inspect-1.0 /usr/local/lib/gstreamer-1.0/libgstrknn.so
```

## 🔍 开发调试

### 调试工具
- **gdb-multiarch**: 多架构调试器
- **strace**: 系统调用跟踪
- **file**: 文件类型检测

### 性能优化
- 使用`-j$(nproc)`并行编译
- 启用Docker构建缓存
- 使用ccache加速重复编译

## 📝 注意事项

1. **架构限制**: 仅支持aarch64架构，不生成x86_64版本
2. **库文件**: 使用项目自带的thirdparty RK3588库
3. **部署**: 自动部署到`/usr/local/lib/gstreamer-1.0/`
4. **缓存**: 构建缓存保留在`./build-cache/`目录
5. **网络**: 确保mDNS正常工作以发现目标设备
