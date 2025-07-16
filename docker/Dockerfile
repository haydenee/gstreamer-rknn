# RK3588交叉编译Docker镜像
FROM ubuntu:22.04

# 避免交互式配置
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai
ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8

# 创建非root用户（与宿主机用户ID匹配）
ARG USER_ID=1000
ARG GROUP_ID=1000
ARG USER_NAME=developer

# 设置apt缓存以加速重建
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && apt-get install -y \
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
    rsync \
    openssh-client \
    # 交叉编译工具链
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    binutils-aarch64-linux-gnu \
    # 构建工具
    build-essential \
    cmake \
    pkg-config \
    autoconf \
    automake \
    libtool \
    meson \
    ninja-build \
    # Python开发
    python3 \
    python3-pip \
    python3-dev \
    python3-setuptools \
    # GStreamer交叉编译依赖（开发头文件）
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libgstreamer-plugins-bad1.0-dev \
    # 交叉编译支持
    libc6-dev-arm64-cross \
    libstdc++6-arm64-cross \
    libgcc-s1-arm64-cross \
    # 开发调试工具
    gdb-multiarch \
    file \
    # 清理apt缓存（由Docker自动处理）
    && true

# 创建非root用户
RUN groupadd -g ${GROUP_ID} ${USER_NAME} && \
    useradd -u ${USER_ID} -g ${GROUP_ID} -m -s /bin/bash ${USER_NAME} && \
    echo "${USER_NAME} ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

# 设置交叉编译环境变量
ENV CROSS_COMPILE=aarch64-linux-gnu-
ENV CC=aarch64-linux-gnu-gcc
ENV CXX=aarch64-linux-gnu-g++
ENV AR=aarch64-linux-gnu-ar
ENV STRIP=aarch64-linux-gnu-strip
ENV PKG_CONFIG_SYSROOT_DIR=/usr/aarch64-linux-gnu
ENV PKG_CONFIG_PATH=/usr/lib/aarch64-linux-gnu/pkgconfig

# 创建交叉编译工作目录
WORKDIR /workspace

# 复制RK3588专用库和头文件到系统目录
COPY --chown=${USER_ID}:${GROUP_ID} thirdparty/ /opt/rk3588-libs/

# 设置RK3588库路径
RUN ln -sf /opt/rk3588-libs/libs/librga.so /usr/lib/aarch64-linux-gnu/librga.so && \
    ln -sf /opt/rk3588-libs/libs/librknnrt.so /usr/lib/aarch64-linux-gnu/librknnrt.so && \
    mkdir -p /usr/lib/aarch64-linux-gnu/pkgconfig && \
    cp /opt/rk3588-libs/pkgconfig/*.pc /usr/lib/aarch64-linux-gnu/pkgconfig/ 2>/dev/null || true

# 设置环境变量
ENV PATH="/home/${USER_NAME}/.local/bin:$PATH"
ENV PYTHONPATH="/workspace:$PYTHONPATH"
ENV LD_LIBRARY_PATH="/opt/rk3588-libs/libs:$LD_LIBRARY_PATH"

# 切换到非root用户
USER ${USER_NAME}

# 默认启动命令
CMD ["/bin/bash"]