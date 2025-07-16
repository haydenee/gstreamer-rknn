# Ubuntu 22.04 LTS Docker镜像用于gstreamer-rknn开发
FROM ubuntu:22.04

# 避免交互式配置
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

# 创建非root用户（与宿主机用户ID匹配）
ARG USER_ID=1000
ARG GROUP_ID=1000
ARG USER_NAME=developer

# 安装基础工具和开发环境
RUN apt-get update && apt-get install -y \
    # 基础工具
    sudo \
    curl \
    wget \
    git \
    vim \
    nano \
    htop \
    tree \
    unzip \
    zip \
    # 构建工具
    build-essential \
    cmake \
    pkg-config \
    autoconf \
    automake \
    libtool \
    # Python开发
    python3 \
    python3-pip \
    python3-dev \
    # GStreamer开发依赖
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libgstreamer-plugins-bad1.0-dev \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav \
    gstreamer1.0-tools \
    gstreamer1.0-x \
    gstreamer1.0-alsa \
    gstreamer1.0-gl \
    gstreamer1.0-gtk3 \
    gstreamer1.0-qt5 \
    gstreamer1.0-pulseaudio \
    # OpenCV（可选）
    libopencv-dev \
    # 其他依赖
    libdrm-dev \
    libssl-dev \
    libffi-dev \
    libjpeg-dev \
    libpng-dev \
    libtiff-dev \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswscale-dev \
    # 清理缓存
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# 创建非root用户
RUN groupadd -g ${GROUP_ID} ${USER_NAME} && \
    useradd -u ${USER_ID} -g ${GROUP_ID} -m -s /bin/bash ${USER_NAME} && \
    echo "${USER_NAME} ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

# 设置工作目录
WORKDIR /workspace

# 切换到非root用户
USER ${USER_NAME}

# 设置环境变量
ENV PATH="/home/${USER_NAME}/.local/bin:$PATH"
ENV PYTHONPATH="/workspace:$PYTHONPATH"

# 默认shell
CMD ["/bin/bash"]