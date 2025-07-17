#!/bin/bash
set -e

# RK3588交叉编译构建脚本
# 使用方法: ./build.sh [clean|build|deploy]

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build-aarch64"
TARGET_DEVICE="${TARGET_DEVICE:-opi-003}"
SSH_USER="${SSH_USER:-root}"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log() {
    echo -e "${GREEN}[$(date +'%Y-%m-%d %H:%M:%S')] $1${NC}"
}

warn() {
    echo -e "${YELLOW}[WARN] $1${NC}"
}

error() {
    echo -e "${RED}[ERROR] $1${NC}"
    exit 1
}

# 检查是否在Docker容器中
check_docker() {
    if [ ! -f /.dockerenv ]; then
        error "此脚本必须在Docker容器中运行！请使用: docker compose run --rm gstreamer-rknn-cross ./docker/build.sh"
    fi
}

# 检查目标设备是否可达
check_target() {
    log "检查目标设备 $TARGET_DEVICE 是否可达..."
    if ! ssh -o ConnectTimeout=5 "$SSH_USER@$TARGET_DEVICE" "echo 'Target reachable'" >/dev/null 2>&1; then
        error "无法连接到目标设备 $TARGET_DEVICE！请确保设备在线且SSH可达。"
    fi
    log "目标设备 $TARGET_DEVICE 可达 ✓"
}

# 清理构建目录
clean_build() {
    log "清理构建目录..."
    rm -rf "$BUILD_DIR"
    log "构建目录已清理 ✓"
}

# 配置构建
configure_build() {
    log "配置交叉编译构建..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    # 使用交叉编译配置
    meson setup .. \
        --cross-file "$SCRIPT_DIR/meson-cross-aarch64.txt" \
        --buildtype=release \
        --prefix=/usr/local
    
    log "构建配置完成 ✓"
}

# 执行构建
do_build() {
    log "开始交叉编译..."
    cd "$BUILD_DIR"
    
    # 并行编译
    ninja -j$(nproc)
    
    log "交叉编译完成 ✓"
}

# 测试构建结果
test_build() {
    log "验证构建结果..."
    
    # 检查生成的文件是否为aarch64
    if [ -f "$BUILD_DIR/src/libgstrknn.so" ]; then
        file_output=$(file "$BUILD_DIR/src/libgstrknn.so")
        if [[ $file_output == *"aarch64"* ]]; then
            log "验证: 生成的库为aarch64架构 ✓"
        else
            error "验证失败: 生成的库不是aarch64架构！"
        fi
    else
        warn "未找到生成的库文件，可能构建失败"
    fi
}

# 部署到目标设备
deploy() {
    log "部署到目标设备 $TARGET_DEVICE..."
    
    check_target
    
    # 创建远程目录
    ssh "$SSH_USER@$TARGET_DEVICE" "mkdir -p /usr/local/lib/gstreamer-1.0"
    
    # 部署插件
    rsync -avz --progress \
        "$BUILD_DIR/src/libgstrknn.so" \
        "$SSH_USER@$TARGET_DEVICE:/usr/local/lib/gstreamer-1.0/"
    
    # 部署RK3588库（如果需要）
    rsync -avz --progress \
        "$PROJECT_ROOT/thirdparty/librga/libs/librga.so" \
        "$PROJECT_ROOT/thirdparty/librknn_api/libs/librknnrt.so" \
        "$SSH_USER@$TARGET_DEVICE:/usr/local/lib/"
    
    # 更新库缓存
    ssh "$SSH_USER@$TARGET_DEVICE" "ldconfig"
    
    log "部署完成 ✓"
}

# 显示使用方法
usage() {
    echo "RK3588交叉编译构建脚本"
    echo ""
    echo "使用方法: $0 [command]"
    echo ""
    echo "命令:"
    echo "  clean   - 清理构建目录"
    echo "  build   - 执行交叉编译"
    echo "  deploy  - 部署到目标设备"
    echo "  all     - 完整流程: 清理+构建+部署"
    echo ""
    echo "环境变量:"
    echo "  TARGET_DEVICE - 目标设备地址 (默认: opi-003)"
    echo "  SSH_USER      - SSH用户名 (默认: root)"
}

# 主逻辑
main() {
    check_docker
    
    case "${1:-build}" in
        clean)
            clean_build
            ;;
        build)
            configure_build
            do_build
            test_build
            ;;
        deploy)
            check_target
            deploy
            ;;
        all)
            clean_build
            configure_build
            do_build
            test_build
            deploy
            ;;
        *)
            usage
            exit 1
            ;;
    esac
    
    log "操作完成！"
}

# 如果直接运行脚本
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi