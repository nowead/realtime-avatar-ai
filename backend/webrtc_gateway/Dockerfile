# syntax=docker/dockerfile:1.4

ARG TARGET_ARCH
FROM --platform=linux/${TARGET_ARCH} ubuntu:22.04 AS webrtc-base

ARG TARGET_ARCH
# APT 캐시 마운트 적용 및 패키지 설치
RUN --mount=type=cache,id=apt-lists-${TARGET_ARCH},sharing=locked,target=/var/lib/apt/lists \
    --mount=type=cache,id=apt-cache-${TARGET_ARCH},sharing=locked,target=/var/cache/apt \
    rm -f /etc/apt/apt.conf.d/docker-clean \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      git python3 python3-distutils build-essential pkg-config curl gnupg ca-certificates dos2unix python3-httplib2\
 && rm -rf /var/lib/apt/lists/*

RUN --mount=type=cache,id=depot_tools_cache,target=/tmp/depot_tools_cache \
    if [ ! -d /tmp/depot_tools_cache/.git ]; then \
      git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git /tmp/depot_tools_cache; \
    else \
      (cd /tmp/depot_tools_cache && git fetch origin && git reset --hard origin/main && git clean -fdx); \
    fi \
 && mkdir -p /opt \
 && cp -a /tmp/depot_tools_cache /opt/depot_tools \
 && find /opt/depot_tools -type f -exec dos2unix {} +

ENV PATH="/opt/depot_tools:$PATH"
ENV PYTHON3="/opt/depot_tools/python3_bin/python3"
ENV GCLIENT_PY3=1

RUN --mount=type=cache,id=webrtc_src,target=/webrtc \
    mkdir -p /webrtc \
 && cd /webrtc \
 && fetch --nohooks webrtc

RUN --mount=type=cache,id=webrtc_src,target=/webrtc \
    cd /webrtc/src \
 && python3 /opt/depot_tools/gclient.py config --spec "solutions = [ \
      { \
        \"name\": \"src\", \
        \"url\": \"https://webrtc.googlesource.com/src.git\", \
        \"managed\": False, \
        \"custom_deps\": {}, \
        \"custom_vars\": { \
          \"checkout_pgo_profiles\": False, \
          \"checkout_clang_coverage_tools\": False, \
          \"checkout_ios_webkit\": False, \
          \"checkout_rtti\": True, \
        }, \
      }, \
    ]; \
    target_os = [\"linux\"]; \
    target_cpu = [\"${TARGET_ARCH}\"];" \
 && python3 /opt/depot_tools/gclient.py sync --reset --force --delete_unversioned_trees --with_branch_heads --with_tags --jobs 16

ENV PATH="/webrtc/src/third_party/llvm-build/Release+Asserts/bin:${PATH}"

RUN --mount=type=cache,id=webrtc_src,target=/webrtc \
    cd /webrtc/src \
 && echo "======================================================================" \
 && echo ">>> DEBUG [gn gen]: Dockerfile ARG TARGET_ARCH received: [${TARGET_ARCH}]" \
 && echo "======================================================================" \
 && _WEBRTC_TARGET_CPU="" \
 && if [ "${TARGET_ARCH}" = "x64" ]; then \
      _WEBRTC_TARGET_CPU="x64"; \
      echo ">>> DEBUG [gn gen]: Condition matched: TARGET_ARCH is x64. _WEBRTC_TARGET_CPU set to [${_WEBRTC_TARGET_CPU}]"; \
    elif [ "${TARGET_ARCH}" = "arm64" ]; then \
      _WEBRTC_TARGET_CPU="arm64"; \
      echo ">>> DEBUG [gn gen]: Condition matched: TARGET_ARCH is arm64. _WEBRTC_TARGET_CPU set to [${_WEBRTC_TARGET_CPU}]"; \
    else \
      echo ">>> ERROR [gn gen]: Unsupported TARGET_ARCH: [${TARGET_ARCH}]. Expected 'x64' or 'arm64'." >&2; \
      echo "======================================================================" >&2; \
      exit 1; \
    fi \
 && if [ -z "${_WEBRTC_TARGET_CPU}" ]; then \
      echo ">>> CRITICAL ERROR [gn gen]: _WEBRTC_TARGET_CPU is empty. This should not happen if TARGET_ARCH was valid." >&2; \
      echo ">>> CRITICAL ERROR [gn gen]: Original TARGET_ARCH was [${TARGET_ARCH}]." >&2; \
      echo "======================================================================" >&2; \
      exit 1; \
    fi \
 && echo ">>> DEBUG [gn gen]: Final _WEBRTC_TARGET_CPU for gn: [${_WEBRTC_TARGET_CPU}]" \
 && GN_ARGS="is_debug=false proprietary_codecs=false rtc_use_h264=false rtc_include_tests=false use_custom_libcxx=false treat_warnings_as_errors=false target_os=\"linux\" target_cpu=\"${_WEBRTC_TARGET_CPU}\" host_os=\"linux\" host_cpu=\"${_WEBRTC_TARGET_CPU}\"" \
 && echo ">>> DEBUG [gn gen]: Full gn args string: [${GN_ARGS}]" \
 && echo "======================================================================" \
 && gn gen out/Default --args="${GN_ARGS}" \
 && ninja -v -C out/Default :webrtc

##############################################
# Builder Stage: 애플리케이션 빌드
##############################################
ARG TARGET_ARCH
FROM --platform=linux/${TARGET_ARCH} webrtc-base AS builder

ENV TARGET_ARCH=${TARGET_ARCH} \
    DEBIAN_FRONTEND=noninteractive \
    CCACHE_DIR=/root/.ccache \
    PKG_CONFIG_PATH=/usr/local/webrtc_custom/lib/pkgconfig:/webrtc/src/out/Default/pkgconfig \
    PATH="/opt/depot_tools:/webrtc/src/third_party/llvm-build/Release+Asserts/bin:${PATH}"

# Builder 스테이지의 APT 설치에도 캐시 마운트 적용
RUN --mount=type=cache,id=apt-lists-builder-${TARGET_ARCH},sharing=locked,target=/var/lib/apt/lists \
    --mount=type=cache,id=apt-cache-builder-${TARGET_ARCH},sharing=locked,target=/var/cache/apt \
    rm -f /etc/apt/apt.conf.d/docker-clean \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      ccache cmake pkg-config \
      libssl-dev libprotobuf-dev protobuf-compiler protobuf-compiler-grpc \
      libgrpc++-dev \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . /app

RUN mkdir -p build \
 && cd build \
 && cmake .. \
      -DENABLE_WEBRTC=ON \
      -DWEBRTC_INCLUDE_DIRS=/webrtc/src \
      -DWEBRTC_LIBRARIES=/webrtc/src/out/Default/obj/libwebrtc.a \
      -DTARGET_ARCH=${TARGET_ARCH} \
 && make -j"$(nproc)"

##############################################
# Runtime Stage: 최소 런타임 환경
##############################################
ARG TARGET_ARCH
FROM --platform=linux/${TARGET_ARCH} ubuntu:22.04 AS runtime

ENV TARGET_ARCH=${TARGET_ARCH} \
    DEBIAN_FRONTEND=noninteractive

# Runtime 스테이지의 APT 설치에도 캐시 마운트 적용
RUN --mount=type=cache,id=apt-lists-runtime-${TARGET_ARCH},sharing=locked,target=/var/lib/apt/lists \
    --mount=type=cache,id=apt-cache-runtime-${TARGET_ARCH},sharing=locked,target=/var/cache/apt \
    rm -f /etc/apt/apt.conf.d/docker-clean \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates libssl3 libprotobuf23 libgrpc++1 wget \
 && rm -rf /var/lib/apt/lists/*

RUN GRPC_HEALTH_PROBE_VERSION=v0.4.26 \
 && PROBE_ARCH="amd64" \
 && if [ "${TARGET_ARCH}" = "arm64" ]; then PROBE_ARCH="arm64"; fi \
 && wget -qO/usr/local/bin/grpc_health_probe \
       https://github.com/grpc-ecosystem/grpc-health-probe/releases/download/${GRPC_HEALTH_PROBE_VERSION}/grpc_health_probe-linux-${PROBE_ARCH} \
 && chmod +x /usr/local/bin/grpc_health_probe \
 && apt-get purge -y --auto-remove wget

WORKDIR /app
COPY --from=builder /app/build/avatar_sync_server .

EXPOSE 50053
CMD ["./avatar_sync_server"]