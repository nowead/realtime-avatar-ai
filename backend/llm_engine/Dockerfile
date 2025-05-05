# llm_engine/Dockerfile
# Base Image: Ubuntu 22.04 LTS

# ---- Builder Stage ----
# 빌드 도구, gRPC/Protobuf, HTTP 클라이언트 의존성 설치 후 소스 코드 컴파일
FROM ubuntu:22.04 AS builder

# 빌드 인자 (필요시 사용)
ARG TARGET_ARCH
ENV TARGET_ARCH=${TARGET_ARCH}

# Non-interactive apt-get
ENV DEBIAN_FRONTEND=noninteractive

# 빌드에 필요한 패키지 설치
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    # C++ 컴파일러, CMake, PkgConfig, Git (FetchContent용)
    build-essential \
    cmake \
    pkg-config \
    git \
    # 파일 다운로드 (FetchContent 내부에서 사용될 수 있음)
    wget \
    # HTTPS 통신 및 인증서 (OpenAI API 호출용)
    ca-certificates \
    # gRPC 빌드 의존성
    libprotobuf-dev \
    protobuf-compiler \
    libgrpc++-dev \
    protobuf-compiler-grpc \
    # cpr/curl 의존성 (HTTPS 필수)
    libssl-dev \
    libcurl4-openssl-dev \
    zlib1g-dev \
    # 패키지 정리
    && update-ca-certificates \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# 소스 코드 복사
WORKDIR /app
COPY . .

# 프로젝트 빌드 (CMakeLists.txt에서 FetchContent로 cpr, json 관리)
RUN mkdir build && cd build && \
    cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DTARGET_ARCH=${TARGET_ARCH} \
    && make VERBOSE=1 -j$(nproc)

# ---- Runtime Stage ----
# 실행에 필요한 최소한의 구성 요소만 포함하는 최종 이미지
FROM ubuntu:22.04 AS runtime

ARG TARGET_ARCH
ENV TARGET_ARCH=${TARGET_ARCH}

# Non-interactive apt-get
ENV DEBIAN_FRONTEND=noninteractive
# OpenSSL 인증서 경로 (표준 경로 사용)
# ENV SSL_CERT_DIR=/etc/ssl/certs

# 런타임 필수 패키지 설치
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    # HTTPS 통신 및 인증서
    ca-certificates \
    libssl3 \
    # gRPC 런타임 의존성
    libprotobuf-lite* \
    libgrpc++1 \
    # cpr/curl 런타임 의존성
    libcurl4 \
    zlib1g \
    # 패키지 정리
    && update-ca-certificates \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# 필요한 공유 라이브러리 복사 (FetchContent가 빌드한 라이브러리 포함 - 필요시)
# cpr과 nlohmann/json은 헤더 위주이거나 정적 링크될 수 있어,
# 시스템 라이브러리(libcurl4, libssl3 등) 외에 추가 복사가 필요 없을 수 있음.
# 만약 동적 라이브러리 이슈 발생 시, builder 스테이지의 /usr/local/lib 등에서 필요한 .so 파일 복사 및 ldconfig 실행
# RUN ldconfig # 필요시 활성화

# 애플리케이션 작업 디렉토리 설정
WORKDIR /app

# 빌더 스테이지에서 컴파일된 실행 파일 복사
COPY --from=builder /app/build/llm_server .

# gRPC 서비스 포트 노출 (llm_engine 기본 포트 50052 가정)
EXPOSE 50052

# 컨테이너 시작 시 기본 명령어 (llm_server 실행)
CMD ["./llm_server"]