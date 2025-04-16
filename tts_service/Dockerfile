FROM ubuntu:22.04

# -----------------------
# 기본 패키지 설치
# -----------------------
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    curl \
    wget \
    unzip \
    pkg-config \
    lsb-release \
    nano \
    libtool \
    autoconf \
    automake \
    ca-certificates \
    libgflags-dev \
    libgtest-dev \
    libssl-dev \
    zlib1g-dev \
    libprotobuf-dev \
    protobuf-compiler \
    libprotoc-dev \
    protobuf-compiler-grpc \
    libgrpc++-dev \
    libgrpc-dev \
    python3 \
    python3-pip

# -----------------------
# CMake 최신 버전 (선택)
# -----------------------
# RUN wget https://github.com/Kitware/CMake/releases/download/v3.28.3/cmake-3.28.3-linux-x86_64.sh \
#     && chmod +x cmake-3.28.3-linux-x86_64.sh \
#     && ./cmake-3.28.3-linux-x86_64.sh --skip-license --prefix=/usr/local

# -----------------------
# grpc_cpp_plugin 설치 확인
# -----------------------
RUN which grpc_cpp_plugin

# -----------------------
# 작업 디렉토리
# -----------------------
WORKDIR /workspace
COPY . /workspace

# -----------------------
# 빌드 예시 (생략 가능)
# -----------------------
# RUN mkdir -p build && cd build && cmake .. && make -j$(nproc)

CMD ["/bin/bash"]
