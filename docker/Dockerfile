FROM ubuntu:22.04

# 设置环境变量
ENV DEBIAN_FRONTEND=noninteractive
ENV LANG=C.UTF-8
ENV TZ=Asia/Shanghai

# 设置交叉编译目标架构
ENV TARGET_ARCH=aarch64-linux-gnu
ENV SYSROOT=/usr/${TARGET_ARCH}

# 接收构建参数（用于创建非root用户）
ARG USER_ID=1000
ARG GROUP_ID=1000
ARG USER_NAME=developer

# 配置多架构支持
RUN rm -f /etc/apt/sources.list && \
    echo "# AMD64架构源" >> /etc/apt/sources.list && \
    echo "deb [arch=amd64] http://archive.ubuntu.com/ubuntu/ jammy main restricted universe multiverse" >> /etc/apt/sources.list && \
    echo "deb [arch=amd64] http://archive.ubuntu.com/ubuntu/ jammy-updates main restricted universe multiverse" >> /etc/apt/sources.list && \
    echo "deb [arch=amd64] http://archive.ubuntu.com/ubuntu/ jammy-backports main restricted universe multiverse" >> /etc/apt/sources.list && \
    echo "deb [arch=amd64] http://security.ubuntu.com/ubuntu/ jammy-security main restricted universe multiverse" >> /etc/apt/sources.list && \
    echo "" >> /etc/apt/sources.list && \
    echo "# ARM64架构源" >> /etc/apt/sources.list && \
    echo "deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports/ jammy main restricted universe multiverse" >> /etc/apt/sources.list && \
    echo "deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports/ jammy-updates main restricted universe multiverse" >> /etc/apt/sources.list && \
    echo "deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports/ jammy-backports main restricted universe multiverse" >> /etc/apt/sources.list && \
    echo "deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports/ jammy-security main restricted universe multiverse" >> /etc/apt/sources.list

# 步骤1: 安装基础系统工具
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && apt-get install -y \
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
    avahi-utils \
    libnss-mdns \
    libnss-resolve

# 步骤2: 安装交叉编译工具链
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && apt-get install -y \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    binutils-aarch64-linux-gnu \
    build-essential \
    cmake \
    pkg-config \
    autoconf \
    automake \
    libtool \
    gdb-multiarch \
    file \
    libc6-dev-arm64-cross \
    libstdc++6-arm64-cross \
    libgcc-s1-arm64-cross

# 步骤3: 安装构建工具
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && apt-get install -y \
    meson \
    ninja-build \
    python3 \
    python3-pip \
    python3-dev \
    python3-setuptools

# 步骤4: 安装ARM64架构的开发包（用于交叉编译头文件和库）
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    dpkg --add-architecture arm64 && \
    apt-get update && apt-get install -y \
    libgstreamer1.0-dev:arm64 \
    libgstreamer-plugins-base1.0-dev:arm64 \
    libgstreamer-plugins-bad1.0-dev:arm64 \
    libglib2.0-dev:arm64 \
    libcairo2-dev:arm64 \
    libopencv-dev:arm64 \
    libgstreamer-plugins-good1.0-dev:arm64 \
    gstreamer1.0-plugins-base:arm64 \
    gstreamer1.0-plugins-good:arm64 \
    gstreamer1.0-plugins-bad:arm64 \
    libglib2.0-dev-bin:arm64

# 步骤5: 创建ARM64库目录结构
RUN mkdir -p /opt/arm64-libs/usr/lib/aarch64-linux-gnu && \
    mkdir -p /opt/arm64-libs/usr/include


# 步骤6: 安装其它开发环境工具（网络工具、磁盘工具等）
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && apt-get install -y \
    # 网络工具
    iputils-ping \
    net-tools \
    dnsutils \
    # 磁盘工具
    ncdu \
    # 进程和系统监控工具
    procps \
    psmisc \
    lsof \
    strace \
    gdb \
    # 其他实用工具
    jq \
    less \
    man-db \
    bc

# 安装meson交叉编译配置文件
COPY docker/meson-cross-aarch64.txt /opt/meson-cross-aarch64.txt


# 创建非root用户
RUN groupadd -g ${GROUP_ID} ${USER_NAME} && \
    useradd -u ${USER_ID} -g ${GROUP_ID} -m -s /bin/bash ${USER_NAME} && \
    echo "${USER_NAME} ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

# 设置工作目录
WORKDIR /workspace

# 创建必要的目录
RUN mkdir -p /workspace/build /workspace/install /workspace/thirdparty

# 复制第三方库
COPY thirdparty/ /workspace/thirdparty/

# 设置环境变量
ENV PATH="/workspace/install/bin:$PATH"
ENV LD_LIBRARY_PATH="/workspace/install/lib:/workspace/thirdparty/lib:$LD_LIBRARY_PATH"
ENV LIBRARY_PATH="/workspace/install/lib:/workspace/thirdparty/lib:$LIBRARY_PATH"
ENV PKG_CONFIG_PATH=/usr/lib/${TARGET_ARCH}/pkgconfig:/usr/share/pkgconfig
ENV PKG_CONFIG_SYSROOT_DIR=/
ENV ARM64_LIB_PATH=/opt/arm64-libs/usr/lib/aarch64-linux-gnu

# 切换到非root用户
USER ${USER_NAME}


# 默认命令
CMD ["/bin/bash"]