# Makefile for webRTC_gateway

# === Variables ===

# 아키텍처 설정: 기본은 시스템 감지, 사용자 지정 가능
TARGET_ARCH ?= $(shell uname -m)

ifeq ($(TARGET_ARCH),x86_64)
	TARGET_ARCH := x64
else ifeq ($(TARGET_ARCH),aarch64)
	TARGET_ARCH := arm64
else ifeq ($(TARGET_ARCH),arm64)
	TARGET_ARCH := arm64
else
	$(warning "Unsupported architecture: $(TARGET_ARCH). Falling back to x64.")
	TARGET_ARCH := x64
endif

# Docker 이미지 및 파일 이름 설정
WEBRTC_GATEWAY_IMAGE_NAME := webrtc-gateway
WEBRTC_GATEWAY_DOCKERFILE := Dockerfile

# Docker Compose 파일 (필요한 경우)
COMPOSE_FILE := docker-compose.yml # webRTC_gateway용 docker-compose.yml 필요

# .env 파일 (필요한 경우)
ENV_FILE := .env

# === Targets ===

.PHONY: all build build-webrtc-gateway start stop down clean logs help

all: build
	@echo "✅ Default target 'all' (build) completed."

# 메인 서비스 이미지 빌드
build: build-webrtc-gateway
	@echo "✅ All required Docker images built successfully."

# webRTC_gateway Docker 이미지 빌드
build-webrtc-gateway:
	@echo "🚀 Building WebRTC Gateway image (${WEBRTC_GATEWAY_IMAGE_NAME}) for TARGET_ARCH: ${TARGET_ARCH}..."
	docker build \
		--build-arg TARGET_ARCH=${TARGET_ARCH} \
		-t ${WEBRTC_GATEWAY_IMAGE_NAME} \
		-f ${WEBRTC_GATEWAY_DOCKERFILE} .

# 서비스 시작 (detached mode) - docker-compose 사용 (필요한 경우)
start:
	@echo "🚀 Starting WebRTC Gateway service (and any dependencies) in detached mode (TARGET_ARCH: ${TARGET_ARCH})..."
	TARGET_ARCH=${TARGET_ARCH} docker compose -f ${COMPOSE_FILE} --env-file ${ENV_FILE} up -d --build webrtc-gateway
	@echo "✅ Services started. Use 'make logs' or 'make stop'."

# 서비스 중지 및 컨테이너 제거 - docker-compose 사용 (필요한 경우)
down:
	@echo "🛑 Stopping and removing services defined in ${COMPOSE_FILE}..."
	TARGET_ARCH=${TARGET_ARCH} docker compose -f ${COMPOSE_FILE} --env-file ${ENV_FILE} down -v --remove-orphans
	@echo "✅ Services stopped and removed."

stop: down # Alias

# 빌드 아티팩트 및 Docker 리소스 정리
clean:
	@echo "🧹 Stopping and removing services (if any)..."
	-TARGET_ARCH=${TARGET_ARCH} docker compose -f ${COMPOSE_FILE} --env-file ${ENV_FILE} down -v --remove-orphans
	@echo "🧹 Cleaning up local build artifacts (./build)..."
	rm -rf build
	@echo "🧹 Removing Docker images (${WEBRTC_GATEWAY_IMAGE_NAME})..."
	-docker rmi ${WEBRTC_GATEWAY_IMAGE_NAME}
	@echo "🧹 Pruning unused Docker data (optional, be careful)..."
	-docker system prune -af # 주석 처리: 사용 시 주의
	@echo "✅ Cleanup attempt complete."

# 실행 중인 서비스 로그 확인 - docker-compose 사용 (필요한 경우)
logs:
	@echo "📜 Tailing logs for services defined in ${COMPOSE_FILE}..."
	TARGET_ARCH=${TARGET_ARCH} docker compose -f ${COMPOSE_FILE} --env-file ${ENV_FILE} logs -f --tail=100 webrtc-gateway

# 도움말 메시지
help:
	@echo ""
	@echo "Makefile for WebRTC Gateway"
	@echo "--------------------------"
	@echo "Usage: make [target]"
	@echo ""
	@echo "Variables:"
	@echo "  TARGET_ARCH      (Optional) Target architecture (e.g., x64, arm64). Default: auto-detected."
	@echo "                   Example: make TARGET_ARCH=arm64 build"
	@echo "  ENV_FILE         (Optional) Path to .env file. Default: ./.env"
	@echo ""
	@echo "Main Targets:"
	@echo "  all              Build all (currently just the service)."
	@echo "  build            Alias for 'all'."
	@echo "  build-webrtc-gateway Build the WebRTC Gateway Docker image."
	@echo ""
	@echo "Service Management (Docker Compose - if applicable):"
	@echo "  start            Start WebRTC Gateway service (and dependencies) in detached mode."
	@echo "  stop             Stop and remove running services (alias for 'down')."
	@echo "  down             Stop and remove services, networks, and volumes."
	@echo "  logs             Tail logs from running services (specifically for webrtc-gateway)."
	@echo ""
	@echo "Cleanup:"
	@echo "  clean            Stop services, remove build artifacts, and Docker images."
	@echo ""
	@echo "Other:"
	@echo "  help             Show this help message."
	@echo ""