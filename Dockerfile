# Joshua Project Dockerfile
# Replicates the joshua_setup.sh process for reproducible builds
# Build: docker build -t joshua .
# Run:   docker run -it --rm -v $(pwd):/workspace joshua bazel run launcher:joshua_main

FROM ubuntu:22.04

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

# Set up locale
RUN apt-get update && apt-get install -y locales && \
    locale-gen en_US en_US.UTF-8 && \
    update-locale LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8
ENV LANG=en_US.UTF-8

# Install basic utilities
RUN apt-get update && apt-get install -y \
    curl \
    wget \
    gnupg \
    lsb-release \
    software-properties-common \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# ============================================
# Install ROS2 Humble
# ============================================
RUN curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key -o /usr/share/keyrings/ros-archive-keyring.gpg && \
    echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] http://packages.ros.org/ros2/ubuntu $(. /etc/os-release && echo $UBUNTU_CODENAME) main" | tee /etc/apt/sources.list.d/ros2.list > /dev/null

RUN apt-get update && apt-get install -y \
    ros-humble-desktop \
    python3-rosdep \
    python3-rosinstall \
    python3-rosinstall-generator \
    python3-wstool \
    python3-colcon-common-extensions \
    build-essential \
    && rm -rf /var/lib/apt/lists/*

# Initialize rosdep
RUN rosdep init || true && rosdep update --rosdistro=humble

# ============================================
# Install OpenCV (x86_64)
# ============================================
RUN apt-get update && apt-get install -y \
    libopencv-dev \
    && rm -rf /var/lib/apt/lists/*

# ============================================
# Install OpenCV ARM64 libraries for cross-compilation
# ============================================
ENV OPENCV_VERSION="4.5.4+dfsg-9ubuntu4"
RUN mkdir -p /tmp/opencv-arm64-install && \
    cd /tmp/opencv-arm64-install && \
    wget -q "http://ports.ubuntu.com/pool/universe/o/opencv/libopencv-core4.5d_${OPENCV_VERSION}_arm64.deb" && \
    wget -q "http://ports.ubuntu.com/pool/universe/o/opencv/libopencv-imgproc4.5d_${OPENCV_VERSION}_arm64.deb" && \
    wget -q "http://ports.ubuntu.com/pool/universe/o/opencv/libopencv-imgcodecs4.5d_${OPENCV_VERSION}_arm64.deb" && \
    wget -q "http://ports.ubuntu.com/pool/universe/o/opencv/libopencv-highgui4.5d_${OPENCV_VERSION}_arm64.deb" && \
    wget -q "http://ports.ubuntu.com/pool/universe/o/opencv/libopencv-videoio4.5d_${OPENCV_VERSION}_arm64.deb" && \
    for pkg in *.deb; do dpkg-deb -x "$pkg" extracted/; done && \
    mkdir -p /usr/lib/aarch64-linux-gnu && \
    cp extracted/usr/lib/aarch64-linux-gnu/libopencv*.so* /usr/lib/aarch64-linux-gnu/ && \
    cd /usr/lib/aarch64-linux-gnu && \
    for lib in libopencv_core libopencv_imgproc libopencv_imgcodecs libopencv_highgui libopencv_videoio; do \
        if [ -f "${lib}.so.4.5d" ]; then ln -sf "${lib}.so.4.5d" "${lib}.so"; fi; \
    done && \
    rm -rf /tmp/opencv-arm64-install

# ============================================
# Install Qt6 development packages
# ============================================
RUN apt-get update && apt-get install -y \
    qt6-base-dev \
    qt6-base-dev-tools \
    qt6-tools-dev \
    qt6-tools-dev-tools \
    && rm -rf /var/lib/apt/lists/*

# ============================================
# Install libevdev for input device support (Xbox controller)
# ============================================
RUN apt-get update && apt-get install -y \
    libevdev-dev \
    && rm -rf /var/lib/apt/lists/*

# ============================================
# Install ARM64 cross-compilation tools
# ============================================
RUN apt-get update && apt-get install -y \
    g++-aarch64-linux-gnu \
    && rm -rf /var/lib/apt/lists/*

# ============================================
# Install Git and Git LFS
# ============================================
RUN apt-get update && apt-get install -y \
    git \
    git-lfs \
    && rm -rf /var/lib/apt/lists/* \
    && git lfs install

# ============================================
# Install Bazelisk (manages Bazel versions via .bazelversion)
# ============================================
ARG TARGETARCH
RUN BAZEL_ARCH=$([ "$TARGETARCH" = "arm64" ] && echo "arm64" || echo "amd64") && \
    curl -L "https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-${BAZEL_ARCH}" -o /usr/local/bin/bazel && \
    chmod +x /usr/local/bin/bazel

# ============================================
# Install linting tools
# ============================================
RUN apt-get update && apt-get install -y \
    clang-format \
    && rm -rf /var/lib/apt/lists/*

# Install buildifier
RUN BUILDIFIER_ARCH=$([ "$(uname -m)" = "aarch64" ] && echo "arm64" || echo "amd64") && \
    curl -L "https://github.com/bazelbuild/buildtools/releases/download/v6.4.0/buildifier-linux-${BUILDIFIER_ARCH}" -o /usr/local/bin/buildifier && \
    chmod +x /usr/local/bin/buildifier

# ============================================
# Install Python tooling
# ============================================
RUN apt-get update && apt-get install -y \
    python3-pip \
    python3-venv \
    && rm -rf /var/lib/apt/lists/* \
    && python3 -m pip install --upgrade pip \
    && python3 -m pip install pre-commit black flake8 isort

# ============================================
# Set up ROS2 environment
# ============================================
ENV ROS_DISTRO=humble
SHELL ["/bin/bash", "-c"]
RUN echo "source /opt/ros/humble/setup.bash" >> /etc/bash.bashrc

# ============================================
# Set working directory
# ============================================
WORKDIR /workspace

# Copy project files (when building with project context)
# For development, you can mount the project directory instead
COPY . /workspace/

# Pre-fetch Bazel dependencies (optional, speeds up subsequent builds)
# Uncomment the next line to cache Bazel deps in the image
# RUN bazel fetch //...

# Default command
CMD ["/bin/bash"]

