#!/bin/bash

# remote_test.sh - 远程测试脚本
# 在 root@opi-003:~/gstreamer-rknn 中执行测试

set -e

# 配置参数
REMOTE_HOST="root@opi-003"
REMOTE_DIR="/root/gstreamer-rknn"
LOG_FILE="/tmp/log"
LOCAL_LOG_FILE="/workspace/log"
TEST_SCRIPT="./script/udp_test.sh"

echo "=== 开始远程测试流程 ==="
echo "远程主机: $REMOTE_HOST"
echo "远程目录: $REMOTE_DIR"


# 1. 使用 rsync 同步本地代码到远程主机
echo "1. 使用 rsync 同步代码到远程主机..."
rsync -avz --exclude='.git' --exclude='docker' --exclude='build*' --exclude='log' --exclude='*.log' ./ $REMOTE_HOST:$REMOTE_DIR/

# 2. 按照 README.md 的提示重新 ninja install（增量编译）
echo "2. 增量构建并安装..."
ssh $REMOTE_HOST "cd $REMOTE_DIR && \
    if [ ! -d 'build' ]; then \
        meson setup build; \
    fi && \
    cd build && \
    ninja && \
    ninja install"

# 3. 执行测试脚本，5秒后强制退出，将 stderr 和 stdout 重定向到 /tmp/log
echo "3. 执行 image_stream_test.sh 测试（5秒后强制退出）..."
ssh $REMOTE_HOST "cd $REMOTE_DIR && timeout 5 $TEST_SCRIPT > $LOG_FILE 2>&1 || true"

# 4. 使用 scp 下载日志文件到本地，并清理控制字符
echo "4. 下载日志文件到本地..."
scp $REMOTE_HOST:$LOG_FILE $LOCAL_LOG_FILE

# 5. 清理控制字符（ANSI转义序列）
echo "5. 清理日志中的控制字符..."
sed -i 's/\x1b\[[0-9;]*m//g' $LOCAL_LOG_FILE

echo "=== 测试完成 ==="
echo "日志已保存到: $LOCAL_LOG_FILE"