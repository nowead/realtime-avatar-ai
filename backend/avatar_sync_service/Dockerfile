# syntax=docker/dockerfile:1.4

FROM ubuntu:22.04 AS webrtc-base

# 1. 필수 패키지 설치
RUN --mount=type=cache,id=apt-lists,target=/var/lib/apt/lists \
    --mount=type=cache,id=apt-cache,target=/var/cache/apt \
    apt-get update && apt-get install -y --no-install-recommends \
    git python3 build-essential pkg-config curl gnupg ca-certificates dos2unix \
    && rm -rf /var/lib/apt/lists/*

# 2. depot_tools 설치 (1회성 clone + 캐시)
RUN --mount=type=cache,id=depot_tools_cache,target=/tmp/depot_tools_cache \
    if [ ! -d /tmp/depot_tools_cache/.git ]; then \
        git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git /tmp/depot_tools_cache; \
    fi && \
    mkdir -p /opt && cp -r /tmp/depot_tools_cache /opt/depot_tools && \
    find /opt/depot_tools -type f -exec dos2unix {} +

ENV PATH="/opt/depot_tools:$PATH"

# 3. fetch (캐시 mount로 /webrtc 유지)
RUN --mount=type=cache,id=webrtc_src,target=/webrtc \
    cd /webrtc && \
    fetch --nohooks webrtc

# 4. gclient sync (캐시 유지)
RUN --mount=type=cache,id=webrtc_src,target=/webrtc \
    cd /webrtc/src && \
    gclient sync

# 5. GN gen (빌드 설정)
RUN --mount=type=cache,id=webrtc_src,target=/webrtc \
    cd /webrtc/src && \
    gn gen out/Default --args="is_debug=false proprietary_codecs=false rtc_use_h264=false"

# 6. ninja 빌드 (libwebrtc → :webrtc로 수정)
RUN --mount=type=cache,id=webrtc_src,target=/webrtc \
    cd /webrtc/src && \
    ninja -C out/Default :webrtc

##############################################
# 1) Builder Stage: Compile avatar_sync_server
##############################################
FROM webrtc-base AS builder

ARG TARGET_ARCH=amd64
ENV TARGET_ARCH=${TARGET_ARCH} \
    DEBIAN_FRONTEND=noninteractive \
    CCACHE_DIR=/root/.ccache \
    PKG_CONFIG_PATH=/usr/local/webrtc_custom/lib/pkgconfig \
    PATH="/opt/depot_tools:$PATH"

RUN apt-get update && apt-get install -y --no-install-recommends \
      ccache cmake pkg-config \
      libssl-dev libprotobuf-dev protobuf-compiler protobuf-compiler-grpc \
      libgrpc++-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . /app
RUN mkdir -p build && cd build \
    && cmake .. -DENABLE_WEBRTC=ON -DWEBRTC_ROOT=/webrtc/src/out/Default \
    && make -j"$(nproc)"

##############################################
# 2) Runtime Stage: Minimal footprint
##############################################
FROM ubuntu:22.04 AS runtime

ARG TARGET_ARCH=amd64
ENV DEBIAN_FRONTEND=noninteractive

# 런타임 의존성 설치
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates libssl3 libprotobuf23 libgrpc++1 wget\
    && rm -rf /var/lib/apt/lists/*

# grpc_health_probe 설치
RUN GRPC_HEALTH_PROBE_VERSION=v0.4.26 && \
    PROBE_ARCH="amd64" && \
    if [ "${TARGET_ARCH}" = "arm64" ]; then PROBE_ARCH="arm64"; fi && \
    wget -qO/usr/local/bin/grpc_health_probe https://github.com/grpc-ecosystem/grpc-health-probe/releases/download/${GRPC_HEALTH_PROBE_VERSION}/grpc_health_probe-linux-${PROBE_ARCH} && \
    chmod +x /usr/local/bin/grpc_health_probe && \
    apt-get purge -y --auto-remove wget

# WebRTC 네이티브 라이브러리 복사 및 링크 설정
COPY --from=webrtc-base /webrtc/src/out/Default/libwebrtc*.so /usr/local/lib/
RUN ldconfig

WORKDIR /app

# 빌드된 바이너리만 복사
COPY --from=builder /app/build/avatar_sync_server .

EXPOSE 50053

CMD ["./avatar_sync_server"]