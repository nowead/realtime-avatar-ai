# Base Image: Ubuntu 22.04 LTS

# ---- Builder Stage ----
# 빌드 도구, SDK 및 필수 라이브러리 설치 후 소스 코드 컴파일
FROM ubuntu:22.04 AS builder

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
    # GStreamer 빌드 의존성 (필요시 유지)
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    # 패키지 정리
    && update-ca-certificates \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Azure Speech SDK 다운로드 및 설치 (항상 최신 버전)
RUN mkdir -p $SPEECHSDK_ROOT && \
    wget -O speechsdk.tar.gz https://aka.ms/csspeech/linuxbinary && \
    tar -xzf speechsdk.tar.gz -C $SPEECHSDK_ROOT --strip-components=1 && \
    rm speechsdk.tar.gz

# 소스 코드 복사
WORKDIR /app
COPY . .

# 프로젝트 빌드
RUN mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -DSPEECHSDK_ROOT=${SPEECHSDK_ROOT} && \
    make VERBOSE=1 -j$(nproc)

# ---- Runtime Stage ----
# 실행에 필요한 최소한의 구성 요소만 포함하는 최종 이미지
FROM ubuntu:22.04 AS runtime

# Non-interactive apt-get
ENV DEBIAN_FRONTEND=noninteractive
ENV SPEECHSDK_ROOT=/opt/speechsdk
# OpenSSL 인증서 경로 지정
# ENV SSL_CERT_DIR=/etc/ssl/certs

# 런타임 필수 패키지 설치
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    # Speech SDK 런타임 의존성
    libasound2 \
    libssl3 \
    ca-certificates \
    # gRPC 런타임 의존성
    libprotobuf-lite* \
    libgrpc++1 \
    # GStreamer 런타임 의존성
    libgstreamer1.0-0 \
    libgstreamer-plugins-base1.0-0 \
    # 패키지 정리
    && update-ca-certificates \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# 빌더 스테이지에서 필요한 SDK 라이브러리 복사
COPY --from=builder ${SPEECHSDK_ROOT}/lib/arm64/libMicrosoft.CognitiveServices.Speech.core.so /usr/local/lib/
COPY --from=builder ${SPEECHSDK_ROOT}/lib/arm64/libpal_azure_c_shared_openssl3.so /usr/local/lib/
COPY --from=builder ${SPEECHSDK_ROOT}/lib/arm64/libMicrosoft.CognitiveServices.Speech.extension.audio.sys.so /usr/local/lib/
# 필요시 다른 확장 라이브러리도 여기에 추가

# 링커 캐시 업데이트
RUN ldconfig

# 애플리케이션 작업 디렉토리 설정
WORKDIR /app

# 빌더 스테이지에서 컴파일된 실행 파일 복사
COPY --from=builder /app/build/stt_server .

# gRPC 서비스 포트 노출
EXPOSE 50055

# 컨테이너 시작 시 기본 명령어 (stt_server 실행)
CMD ["./stt_server"]