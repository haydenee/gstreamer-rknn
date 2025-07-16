# GStreamer-RKNN Docker开发环境

## 概述

本项目提供了基于Docker的GStreamer-RKNN开发环境，使用Ubuntu 22.04 LTS作为基础镜像，预装了完整的GStreamer开发工具链和RKNN相关依赖。

## 环境要求

- Docker Engine 20.10+
- Docker Compose（或Docker内置的compose命令）
- Linux系统（推荐Ubuntu 20.04+或类似系统）

## 快速开始

### 1. 启动开发环境

```bash
cd docker
./start.sh
```

脚本会自动：
- 检查Docker环境
- 创建`.env`配置文件（基于`.env.example`）
- 构建Docker镜像
- 启动容器

### 2. 进入容器

容器启动后，使用以下命令进入：

```bash
# 方法1：使用docker命令
docker exec -it gstreamer-rknn-dev bash

# 方法2：使用docker compose
docker compose exec gstreamer-rknn-dev bash
```

### 3. 验证环境

进入容器后，可以验证开发环境：

```bash
# 检查GStreamer版本
gst-launch-1.0 --version

# 检查RKNN库
ls -la /usr/lib/librknnrt.so

# 检查RGA库
ls -la /usr/lib/librga.so

# 测试编译环境
gcc --version
cmake --version
```

## 目录结构

```
docker/
├── Dockerfile          # Ubuntu 22.04基础镜像定义
├── docker-compose.yml  # Docker Compose配置
├── start.sh           # 一键启动脚本
├── .env.example       # 环境变量模板
├── .env               # 自动生成的环境变量文件（不被git跟踪）
├── cache/             # 容器缓存目录（不被git跟踪）
└── history/           # bash历史记录目录（不被git跟踪）
```

## 环境变量配置

`.env`文件包含以下可配置项：

- `USER_ID`: 宿主机用户ID（默认自动获取）
- `GROUP_ID`: 宿主机用户组ID（默认自动获取）
- `USER_NAME`: 容器内用户名（默认使用当前用户名）

## 功能特性

### ✅ 已安装的开发工具
- **编译工具**: build-essential, cmake, make, gcc, g++
- **版本控制**: git
- **编辑器**: vim, nano
- **网络工具**: curl, wget, net-tools
- **调试工具**: gdb, valgrind

### ✅ GStreamer完整开发环境
- **核心库**: libgstreamer1.0-dev, libgstreamer-plugins-base1.0-dev
- **插件**: good, bad, ugly, libav, x, rtp, rtsp等完整插件包
- **工具**: gstreamer1.0-tools, gstreamer1.0-doc
- **测试工具**: gst-launch-1.0, gst-inspect-1.0

### ✅ RKNN相关依赖
- **RKNN Runtime**: librknnrt.so
- **RGA库**: librga.so（Rockchip图形加速）
- **头文件**: rknn_api.h, rga相关头文件

### ✅ 卷挂载配置
- **项目代码**: 自动挂载到`/workspace`
- **X11转发**: 支持GUI应用显示
- **GPU设备**: 支持/dev/dri设备访问
- **缓存目录**: 持久化用户缓存和bash历史

## 使用技巧

### 1. 构建项目

进入容器后：

```bash
cd /workspace
meson setup build
ninja -C build
```

### 2. 运行测试

```bash
# 运行文件测试
./script/file_test.sh

# 运行流测试
./script/stream_test.sh

# 运行UDP测试
./script/udp_test.sh
```

### 3. 开发调试

- 使用`gdb`调试C/C++程序
- 使用`gst-launch-1.0 -v`查看详细管道信息
- 使用`gst-inspect-1.0`查看插件信息

### 4. 容器管理

```bash
# 停止容器
docker compose down

# 重启容器
docker compose restart

# 查看日志
docker compose logs -f

# 清理所有容器和数据
docker compose down -v
```

## 故障排除

### 问题1: GUI应用无法显示
```bash
# 在宿主机执行
xhost +local:docker
export DISPLAY=:0
```

### 问题2: 权限问题
确保`.env`文件中的`USER_ID`和`GROUP_ID`与宿主机一致：
```bash
id -u  # 查看用户ID
id -g  # 查看组ID
```

### 问题3: Docker构建失败
```bash
# 清理并重新构建
docker compose down
docker compose build --no-cache
docker compose up -d
```

## 注意事项

1. **权限映射**: 容器内用户ID会自动匹配宿主机，避免文件权限问题
2. **代码同步**: 项目目录实时同步，宿主机修改立即生效
3. **缓存持久化**: `.cache`和`.bash_history`会持久化到本地目录
4. **网络模式**: 使用host网络模式，方便调试网络相关功能

## 更新日志

- v1.0.0: 初始版本，基于Ubuntu 22.04 LTS
- 包含完整的GStreamer 1.20+开发环境
- 集成RKNN Runtime和RGA库
- 支持GUI应用和GPU加速