# TTS Service Dockerfile
# Base Image: Ubuntu 22.04 LTS

# ---- Builder Stage ----
# 빌드 도구, SDK 및 필수 라이브러리 설치 후 소스 코드 컴파일
FROM ubuntu:22.04 AS builder

ARG TARGET_ARCH
ENV TARGET_ARCH=${TARGET_ARCH}

# Non-interactive apt-get
ENV DEBIAN_FRONTEND=noninteractive
# Speech SDK 설치 경로
ENV SPEECHSDK_ROOT=/opt/speechsdk

# 빌드에 필요한 패키지 설치
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    # C++ 컴파일러, CMake, PkgConfig
    build-essential \
    cmake \
    pkg-config \
    # 파일 다운로드 및 압축 해제
    wget \
    tar \
    git \
    # HTTPS 통신 및 인증서
    ca-certificates \
    # Speech SDK 필수 빌드 의존성
    libssl-dev \
    libasound2-dev \
    # gRPC 빌드 의존성
    libprotobuf-dev \
    protobuf-compiler \
    libgrpc++-dev \
    protobuf-compiler-grpc \
    # uuid-dev
    uuid-dev \
    # 패키지 정리
    && update-ca-certificates \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Azure Speech SDK 다운로드 및 설치 (항상 최신 버전 다운로드 방식)
RUN mkdir -p ${SPEECHSDK_ROOT} && \
    wget -O speechsdk.tar.gz https://aka.ms/csspeech/linuxbinary && \
    tar -xzf speechsdk.tar.gz -C ${SPEECHSDK_ROOT} --strip-components=1 && \
    rm speechsdk.tar.gz

# 소스 코드 복사
WORKDIR /app
COPY . .

# 프로젝트 빌드 (BUILD_TESTING 옵션은 CMakeLists.txt의 기본값(OFF)을 따름)
RUN mkdir -p build && cd build && \
    cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DSPEECHSDK_ROOT=${SPEECHSDK_ROOT} \
    -DTARGET_ARCH=${TARGET_ARCH} \
    && make VERBOSE=1 -j$(nproc)

# ---- Runtime Stage ----
# 실행에 필요한 최소한의 구성 요소만 포함하는 최종 이미지
FROM ubuntu:22.04 AS runtime

ARG TARGET_ARCH
ENV TARGET_ARCH=${TARGET_ARCH}

# Non-interactive apt-get
ENV DEBIAN_FRONTEND=noninteractive
ENV SPEECHSDK_ROOT=/opt/speechsdk

# 런타임 필수 패키지 설치
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    # Speech SDK 런타임 의존성
    libasound2 \
    libssl3 \
    ca-certificates \
    # gRPC C++ 런타임 라이브러리
    libgrpc++1 \
    libprotobuf-lite23 \
    # uuid 런타임 라이브러리
    libuuid1 \
    # 패키지 정리
    && update-ca-certificates \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# 빌더 스테이지에서 필요한 SDK 라이브러리 복사
COPY --from=builder ${SPEECHSDK_ROOT}/lib/${TARGET_ARCH}/libMicrosoft.CognitiveServices.Speech.core.so /usr/local/lib/
COPY --from=builder ${SPEECHSDK_ROOT}/lib/${TARGET_ARCH}/libpal_azure_c_shared_openssl3.so /usr/local/lib/
COPY --from=builder ${SPEECHSDK_ROOT}/lib/${TARGET_ARCH}/libMicrosoft.CognitiveServices.Speech.extension.audio.sys.so /usr/local/lib/
# OpenSSL 3.x 용 PAL 라이브러리가 있다면 복사 (SDK 버전에 따라 파일명이 다를 수 있음)
# 예시: COPY --from=builder ${SPEECHSDK_ROOT}/lib/${TARGET_ARCH}/libpal_azure_c_shared_openssl3.so /usr/local/lib/

# 링커 캐시 업데이트
RUN ldconfig

# 애플리케이션 작업 디렉토리 설정
WORKDIR /app

# 빌더 스테이지에서 컴파일된 실행 파일 복사
COPY --from=builder /app/build/tts_server .
# 유닛 테스트 실행 파일은 이 Dockerfile로 빌드된 이미지에는 포함되지 않음.

# gRPC 서비스 포트 노출 (TTS 서비스 포트)
EXPOSE 50052

# 컨테이너 시작 시 기본 명령어 (tts_server 실행)
CMD ["./tts_server"]