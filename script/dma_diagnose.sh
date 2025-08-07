#!/bin/bash

# dma_diagnose.sh - DMA诊断脚本
# 在目标设备上收集DMA相关信息

set -e

# 配置参数
REMOTE_HOST="root@opi-003"
REMOTE_DIR="/root/gstreamer-rknn"
LOG_FILE="/tmp/dma_diagnose.log"
LOCAL_LOG_FILE="/workspace/dma_diagnose.log"

echo "=== 开始DMA诊断 ==="
echo "远程主机: $REMOTE_HOST"
echo "远程目录: $REMOTE_DIR"

# 1. 使用 rsync 同步本地代码到远程主机（如果需要）
echo "1. 同步代码到远程主机..."
rsync -avz --exclude='.git' --exclude='docker' --exclude='build*' --exclude='log' --exclude='*.log' ./ $REMOTE_HOST:$REMOTE_DIR/

# 2. 在远程主机上执行诊断命令
echo "2. 执行DMA诊断命令..."
ssh $REMOTE_HOST "cd $REMOTE_DIR && (
  echo '=== 系统信息 ===' &&
  uname -a &&
  echo '' &&
  
  echo '=== 内存信息 ===' &&
  free -h &&
  echo '' &&
  
  echo '=== DMA Heap信息 ===' &&
  if [ -d '/dev/dma_heap' ]; then
    ls -la /dev/dma_heap/
  else
    echo 'No /dev/dma_heap directory found'
  fi &&
  echo '' &&
  
  echo '=== Meminfo中的DMA信息 ===' &&
  cat /proc/meminfo | grep -i dma || echo 'No DMA info found in /proc/meminfo' &&
  echo '' &&
  
  echo '=== 内核DMA日志 ===' &&
  dmesg | grep -i dma | tail -20 || echo 'No DMA messages in dmesg' &&
  echo '' &&
  
  echo '=== CMA信息 ===' &&
  if [ -f '/proc/cma' ]; then
    cat /proc/cma
  else
    echo 'No /proc/cma file found'
  fi &&
  echo '' &&
  
  echo '=== 系统内存布局 ===' &&
  cat /proc/iomem | grep -i dma || echo 'No DMA regions in /proc/iomem'
) > $LOG_FILE 2>&1"

# 3. 下载日志文件到本地
echo "3. 下载诊断日志到本地..."
scp $REMOTE_HOST:$LOG_FILE $LOCAL_LOG_FILE

echo "=== 诊断完成 ==="
echo "诊断日志已保存到: $LOCAL_LOG_FILE"