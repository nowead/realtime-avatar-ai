# Dockerfile for tts_service (주석 한국어 버전)

# ---- 빌더 스테이지 ----
# 이 단계에서는 빌드 도구, 의존성(OpenSSL 1.1 포함), SDK를 설치하고 프로젝트를 컴파일합니다.
FROM ubuntu:22.04 AS builder

# apt 명령어 실행 시 사용자 입력을 받지 않도록 설정
ENV DEBIAN_FRONTEND=noninteractive
# Speech SDK 루트 디렉토리 정의 및 라이브러리 경로 추가
ENV SPEECHSDK_ROOT=/opt/speechsdk
ENV LD_LIBRARY_PATH="$SPEECHSDK_ROOT/lib/x64:$LD_LIBRARY_PATH"

# 시스템 업데이트 및 공식 문서 권장 사항 기반 필수 패키지 설치
# build-essential, cmake, pkg-config: C++ 컴파일용
# wget, tar: SDK 다운로드 및 압축 해제용
# ca-certificates: HTTPS 연결용
# libssl-dev, libasound2-dev: Linux용 핵심 Speech SDK 의존성
# libgstreamer*: Speech SDK의 포괄적인 오디오 지원을 위해 종종 권장됨
# gRPC 의존성: 사용자 프로젝트용
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    wget \
    tar \
    curl \
    ca-certificates \
    libssl-dev \
    libasound2-dev \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    # gRPC 의존성 설치
    libprotobuf-dev \
    protobuf-compiler \
    libgrpc++-dev \
    protobuf-compiler-grpc \
    # apt 캐시 정리
    && apt-get clean \
    && update-ca-certificates\
    && rm -rf /var/lib/apt/lists/*

# Linux x64용 Azure Speech SDK C++ 다운로드 및 설치
# 공식 샘플/문서 링크: https://aka.ms/csspeech/linuxbinary
RUN mkdir -p $SPEECHSDK_ROOT && \
    wget -O speechsdk.tar.gz https://aka.ms/csspeech/linuxbinary && \
    tar -xzf speechsdk.tar.gz -C $SPEECHSDK_ROOT --strip-components=1 && \
    rm speechsdk.tar.gz

# 애플리케이션 소스 코드 복사
WORKDIR /app
COPY . .

# 프로젝트 빌드
# CMakeLists.txt가 SDK를 찾기 위해 SPEECHSDK_ROOT를 사용한다고 가정
RUN mkdir build && mv proto build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -DSPEECHSDK_ROOT=${SPEECHSDK_ROOT} && \
    make -j$(nproc)

# ---- 런타임 스테이지 ----
# 이 단계에서는 런타임 의존성과 컴파일된 애플리케이션만 포함하는 더 작은 최종 이미지를 만듭니다.
FROM ubuntu:22.04 AS runtime

ENV SPEECHSDK_ROOT=/opt/speechsdk
# apt 명령어 실행 시 사용자 입력을 받지 않도록 설정
ENV DEBIAN_FRONTEND=noninteractive
# 커스텀 OpenSSL 및 SDK 라이브러리 경로를 링커 경로에 추가
ENV LD_LIBRARY_PATH="/usr/local/lib:/usr/local/ssl/lib:$LD_LIBRARY_PATH"

# 런타임 의존성 설치
# libasound2: 오디오 런타임
# libprotobuf-lite*, libgrpc++1: gRPC 런타임
# libgstreamer*: GStreamer 런타임
# ca-certificates: 보안 연결에 필요
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    libasound2 \
    libprotobuf-lite* \
    libgrpc++1 \
    # libgstreamer1.0-0 \
    # libgstreamer-plugins-base1.0-0 \
    ca-certificates \
    libssl3 \
    curl \
    wget \
    # apt 캐시 정리
    && update-ca-certificates \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# 빌더 스테이지에서 필요한 라이브러리 복사
# 수동으로 빌드한 OpenSSL 1.1 라이브러리 복사
# COPY --from=builder /usr/local/ssl/lib/libssl.so.1.1 /usr/local/lib/
# COPY --from=builder /usr/local/ssl/lib/libcrypto.so.1.1 /usr/local/lib/
# Azure Speech SDK C++ 런타임 라이브러리 복사
COPY --from=builder ${SPEECHSDK_ROOT}/lib/x64/libMicrosoft.CognitiveServices.Speech.core.so /usr/local/lib/
# COPY --from=builder ${SPEECHSDK_ROOT}/lib/x64/libMicrosoft.CognitiveServices.Speech.extension.dynamic.so /usr/local/lib/
# 필요한 경우 다른 SDK .so 파일 추가 (예: 오디오 압축 형식용)
# COPY --from=builder ${SPEECHSDK_ROOT}/lib/x64/libMicrosoft.CognitiveServices.Speech.extension.codec.so /usr/local/lib/

# 링커 캐시 업데이트
RUN ldconfig

# 작업 디렉토리 설정
WORKDIR /app

# 빌더 스테이지에서 컴파일된 애플리케이션 실행 파일 복사
COPY --from=builder /app/build/tts_server .

# 서비스에서 사용하는 gRPC 포트 노출
EXPOSE 50055

# 컨테이너 시작 시 TTS 서버를 실행하는 명령어
# 'docker run' 실행 시 -e 플래그를 사용하여 AZURE_SPEECH_KEY와 AZURE_SPEECH_REGION를 제공해야 합니다.
CMD ["./tts_server"]