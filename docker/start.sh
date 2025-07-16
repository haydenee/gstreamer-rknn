#!/bin/bash
# GStreamer-RKNN Docker开发环境启动脚本

set -e

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== GStreamer-RKNN Docker开发环境启动脚本 ===${NC}"

# 检查Docker是否安装
if ! command -v docker &> /dev/null; then
    echo -e "${RED}错误: Docker未安装${NC}"
    exit 1
fi

if ! docker compose version &> /dev/null; then
    echo -e "${RED}错误: Docker Compose未安装${NC}"
    exit 1
fi

# 获取当前用户信息
USER_ID=$(id -u)
GROUP_ID=$(id -g)
USER_NAME=$(whoami)

# 创建.env文件（如果不存在）
if [ ! -f .env ]; then
    echo -e "${YELLOW}创建.env配置文件...${NC}"
    cp .env.example .env
    
    # 更新用户配置
    sed -i "s/USER_ID=1000/USER_ID=$USER_ID/g" .env
    sed -i "s/GROUP_ID=1000/GROUP_ID=$GROUP_ID/g" .env
    sed -i "s/USER_NAME=developer/USER_NAME=$USER_NAME/g" .env
    
    echo -e "${GREEN}.env文件已创建，可根据需要编辑${NC}"
fi

# 创建必要的目录
mkdir -p cache
mkdir -p history

# 检查X11转发
if [ -z "$DISPLAY" ]; then
    echo -e "${YELLOW}警告: DISPLAY环境变量未设置，GUI应用可能无法运行${NC}"
    echo -e "请使用: export DISPLAY=:0 或类似的值"
fi

# 启动容器
echo -e "${GREEN}正在构建Docker镜像...${NC}"
docker compose build

echo -e "${GREEN}正在启动容器...${NC}"
docker compose up -d

echo -e "${GREEN}容器启动成功！${NC}"
echo -e "使用以下命令进入容器："
echo -e "  ${YELLOW}docker exec -it gstreamer-rknn-dev bash${NC}"
echo -e "或者使用："
echo -e "  ${YELLOW}docker compose exec gstreamer-rknn-dev bash${NC}"

# 显示容器状态
docker compose ps