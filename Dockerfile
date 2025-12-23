# High-Performance Trading System Development Environment
# Optimized for low-latency C++ development

FROM ubuntu:22.04

LABEL maintainer="HFT Developer"
LABEL description="Low-latency trading system development environment"

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install essential build tools and libraries
RUN apt-get update && apt-get install -y \
    # Compilers and build tools
    build-essential \
    cmake \
    ninja-build \
    clang-14 \
    clang-tools-14 \
    lldb-14 \
    g++-12 \
    # Performance analysis tools
    linux-tools-generic \
    perf-tools-unstable \
    valgrind \
    google-perftools \
    libgoogle-perftools-dev \
    # Debugging tools
    gdb \
    strace \
    ltrace \
    # Networking libraries
    libboost-all-dev \
    libssl-dev \
    libcurl4-openssl-dev \
    # WebSocket and HTTP libraries
    libwebsocketpp-dev \
    libasio-dev \
    # JSON parsing
    nlohmann-json3-dev \
    # Utilities
    git \
    wget \
    curl \
    htop \
    numactl \
    cpuset \
    vim \
    # Documentation
    doxygen \
    graphviz \
    && rm -rf /var/lib/apt/lists/*

# Set up modern compiler as default
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-12 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-12 100 \
    && update-alternatives --install /usr/bin/clang clang /usr/bin/clang-14 100 \
    && update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-14 100

# Create working directory
WORKDIR /hft

# Copy source code
COPY . /hft

# Set environment variables for performance
ENV CC=gcc
ENV CXX=g++
ENV CMAKE_BUILD_TYPE=Release

# Build the project
RUN mkdir -p build && cd build \
    && cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_FLAGS="-O3 -march=native -mtune=native -flto" \
        .. \
    && ninja

# Default command runs benchmarks
CMD ["./build/bin/benchmark_suite"]

