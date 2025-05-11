#------------------------------------------------------------------------------
# 단계 1: 빌더 (Builder Stage)
#------------------------------------------------------------------------------
FROM ubuntu:22.04 AS builder

LABEL stage="builder"
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        git \
        pkg-config \
        wget \
        ca-certificates \
        libssl-dev \
        zlib1g-dev \
        libprotobuf-dev \
        protobuf-compiler \
        libgrpc++-dev \
        protobuf-compiler-grpc \
    && update-ca-certificates \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# uWebSockets 소스 코드 클론 및 Make 기반 빌드/설치, uSockets 헤더/라이브러리 설치
RUN git clone --branch v20.40.0 --recurse-submodules \
      https://github.com/uNetworking/uWebSockets.git external/uwebsockets && \
    cd external/uwebsockets && \
    make && \
    make install PREFIX=/usr/local && \
    # uSockets 정적 라이브러리 설치
    cp uSockets/uSockets.a /usr/local/lib/libuSockets.a && \
    # uSockets 헤더 설치
    mkdir -p /usr/local/include && \
    cp uSockets/src/*.h /usr/local/include/

COPY CMakeLists.txt ./
COPY protos/ ./protos/
COPY src/ ./src/

RUN mkdir -p build && cd build && \
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
    && make VERBOSE=1 -j$(nproc) && \
    strip WebSocketGateway


#------------------------------------------------------------------------------
# 단계 2: 최종 실행 이미지 (Final Stage)
#------------------------------------------------------------------------------
FROM ubuntu:22.04 AS runtime

LABEL stage="runtime"
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        libssl3 \
        zlib1g \
        ca-certificates \
        libgrpc++1 \
        libprotobuf-lite23 \
    && update-ca-certificates \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /app/build/WebSocketGateway .

ENV WS_PORT="3000"
ENV STT_SERVICE_ADDR="stt-service:50052"
ENV GRPC_AVATAR_SYNC_ADDR="0.0.0.0:50055"

EXPOSE ${WS_PORT}
EXPOSE 50051

CMD ["./WebSocketGateway"]