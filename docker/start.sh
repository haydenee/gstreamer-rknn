#!/bin/bash
set -e

# RK3588交叉编译环境启动脚本

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# 颜色输出
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log() {
    echo -e "${GREEN}[$(date +'%Y-%m-%d %H:%M:%S')] $1${NC}"
}

warn() {
    echo -e "${YELLOW}[WARN] $1${NC}"
}

# 检查Docker环境
check_docker() {
    if ! command -v docker &> /dev/null; then
        echo "错误: Docker未安装！请先安装Docker。"
        exit 1
    fi
    
    if ! docker info &> /dev/null; then
        echo "错误: Docker守护进程未运行！请启动Docker服务。"
        exit 1
    fi
}

# 创建.env文件（如果不存在）
setup_env() {
    if [ ! -f "$SCRIPT_DIR/.env" ]; then
        log "创建环境变量文件..."
        cat > "$SCRIPT_DIR/.env" << EOF
# 用户配置
USER_ID=$(id -u)
GROUP_ID=$(id -g)
USER_NAME=$(whoami)

# 目标设备配置（通过mDNS自动发现）
TARGET_DEVICE=opi-003.local
SSH_USER=root

# 显示配置
DISPLAY=$DISPLAY
EOF
        log ".env文件已创建 ✓"
    fi
}

# 清理旧的交叉编译容器
cleanup_containers() {
    log "清理旧的容器..."
    docker compose -f "$SCRIPT_DIR/docker-compose.yml" down --remove-orphans || true
}

# 构建镜像
build_image() {
    log "构建RK3588交叉编译镜像..."
    docker compose -f "$SCRIPT_DIR/docker-compose.yml" build --no-cache
    log "镜像构建完成 ✓"
}

# 显示使用说明
show_usage() {
    echo ""
    echo "🎯 RK3588交叉编译环境已就绪！"
    echo ""
    echo "使用方法："
    echo "  1. 进入容器："
    echo "     docker compose exec gstreamer-rknn-cross bash"
    echo ""
    echo "  2. 交叉编译："
    echo "     ./docker/build.sh build"
    echo ""
    echo "  3. 一键构建+部署："
    echo "     ./docker/build.sh all"
    echo ""
    echo "  4. 仅部署："
    echo "     ./docker/build.sh deploy"
    echo ""
    echo "  5. 清理构建："
    echo "     ./docker/build.sh clean"
    echo ""
    echo "环境变量可在 .env 文件中配置"
    echo ""
    echo "目标设备：$TARGET_DEVICE (通过mDNS自动发现)"
}

# 主函数
main() {
    log "🚀 启动RK3588交叉编译环境..."
    
    check_docker
    setup_env
    
    # 切换到docker目录
    cd "$SCRIPT_DIR"
    
    cleanup_containers
    
    if [ "$1" == "--build" ]; then
        build_image
    fi
    
    log "启动交叉编译容器..."
    docker compose up -d
    
    # 等待容器启动
    sleep 2
    
    show_usage
}

# 如果直接运行脚本
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi
