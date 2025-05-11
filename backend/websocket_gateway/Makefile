# Makefile for WebSocket Gateway

# === Variables ===

# 아키텍처 설정: 기본은 시스템 감지, 사용자 지정 가능
TARGET_ARCH ?= $(shell uname -m)

ifeq ($(TARGET_ARCH),x86_64)
	TARGET_ARCH := x64
else ifeq ($(TARGET_ARCH),aarch64)
	TARGET_ARCH := arm64
else ifeq ($(TARGET_ARCH),arm64) # 중복된 조건이지만, 명시적으로 arm64를 처리
	TARGET_ARCH := arm64
else
	$(warning "Unsupported architecture: $(TARGET_ARCH). Falling back to x64.")
	TARGET_ARCH := x64
endif

# Docker 이미지 및 파일 이름 설정
WEBSOCKET_GATEWAY_IMAGE_NAME := websocket-gateway
WEBSOCKET_GATEWAY_DOCKERFILE := Dockerfile

# 유닛 테스트 관련 Docker 이미지 및 파일 이름 설정
UNIT_TEST_IMAGE_NAME := websocket-gateway-unittest
UNIT_TEST_DOCKERFILE := Dockerfile.unit_test

COMPOSE_FILE := docker-compose.yml # docker-compose.yml 파일이 있다고 가정

# === Targets ===

.PHONY: all build build-websocket-gateway build-unit-test test start stop down clean logs help

all: build
	@echo "✅ Default target 'all' (build) completed."

# 메인 서비스 이미지 빌드
build: build-websocket-gateway
	@echo "✅ All required Docker images built successfully."

# WebSocket Gateway Docker 이미지 빌드
build-websocket-gateway:
	@echo "🚀 Building WebSocket Gateway image (${WEBSOCKET_GATEWAY_IMAGE_NAME}) for TARGET_ARCH: ${TARGET_ARCH}..."
	docker build \
		--build-arg TARGET_ARCH=${TARGET_ARCH} \
		-t ${WEBSOCKET_GATEWAY_IMAGE_NAME} \
		-f ${WEBSOCKET_GATEWAY_DOCKERFILE} .

# 유닛 테스트 Docker 이미지 빌드
build-unit-test:
	@echo "🧪 Building Unit Test image (${UNIT_TEST_IMAGE_NAME}) for TARGET_ARCH: ${TARGET_ARCH}..."
	docker build \
		--build-arg TARGET_ARCH=${TARGET_ARCH} \
		-t ${UNIT_TEST_IMAGE_NAME} \
		-f ${UNIT_TEST_DOCKERFILE} .

# 유닛 테스트 실행 (빌드된 유닛 테스트 이미지 사용)
test: build-unit-test
	@echo "▶️ Running Unit Tests from image ${UNIT_TEST_IMAGE_NAME}..."
	docker run --rm ${UNIT_TEST_IMAGE_NAME}
	@echo "✅ Unit Tests completed."

# 서비스 시작 (detached mode) - docker-compose 사용
start:
	@echo "🚀 Starting WebSocket Gateway service (and any dependencies) in detached mode (TARGET_ARCH: ${TARGET_ARCH})..."
	@if [ ! -f "${COMPOSE_FILE}" ]; then \
		echo "⚠️ Docker Compose file '${COMPOSE_FILE}' not found. Cannot start services via docker-compose."; \
		exit 1; \
	fi
	@if [ ! -f "${ENV_FILE}" ]; then \
		echo "⚠️ Environment file '${ENV_FILE}' not found. Proceeding without it for docker-compose if possible."; \
	fi
	TARGET_ARCH=${TARGET_ARCH} docker compose -f ${COMPOSE_FILE} $(if [ -f "${ENV_FILE}" ],--env-file ${ENV_FILE}) up -d --build websocket-gateway
	@echo "✅ Services started. Use 'make logs' or 'make stop'."

# 서비스 중지 및 컨테이너 제거
down:
	@echo "🛑 Stopping and removing services defined in ${COMPOSE_FILE}..."
	@if [ ! -f "${COMPOSE_FILE}" ]; then \
		echo "⚠️ Docker Compose file '${COMPOSE_FILE}' not found. Cannot stop services via docker-compose."; \
		exit 0; \
	fi
	TARGET_ARCH=${TARGET_ARCH} docker compose -f ${COMPOSE_FILE} $(if [ -f "${ENV_FILE}" ],--env-file ${ENV_FILE}) down -v --remove-orphans
	@echo "✅ Services stopped and removed."

stop: down # Alias

# 빌드 아티팩트 및 Docker 리소스 정리
clean:
	@echo "🧹 Stopping and removing services (if any)..."
	-@if [ -f "${COMPOSE_FILE}" ]; then \
		TARGET_ARCH=${TARGET_ARCH} docker compose -f ${COMPOSE_FILE} $(if [ -f "${ENV_FILE}" ],--env-file ${ENV_FILE}) down -v --remove-orphans; \
	fi
	@echo "🧹 Cleaning up local build artifacts (./build)..."
	rm -rf build
	@echo "🧹 Removing Docker images (${WEBSOCKET_GATEWAY_IMAGE_NAME}, ${UNIT_TEST_IMAGE_NAME})..."
	-docker rmi ${WEBSOCKET_GATEWAY_IMAGE_NAME} ${UNIT_TEST_IMAGE_NAME}
	@echo "🧹 Pruning unused Docker data (optional, be careful)..."
	# -docker system prune -af # 주석 처리: 사용 시 주의. 필요시 주석 해제.
	@echo "✅ Cleanup attempt complete."

# 실행 중인 서비스 로그 확인
logs:
	@echo "📜 Tailing logs for services defined in ${COMPOSE_FILE}..."
	@if [ ! -f "${COMPOSE_FILE}" ]; then \
		echo "⚠️ Docker Compose file '${COMPOSE_FILE}' not found. Cannot tail logs via docker-compose."; \
		exit 1; \
	fi
	TARGET_ARCH=${TARGET_ARCH} docker compose -f ${COMPOSE_FILE} $(if [ -f "${ENV_FILE}" ],--env-file ${ENV_FILE}) logs -f --tail=100 websocket-gateway

# 도움말 메시지
help:
	@echo ""
	@echo "Makefile for WebSocket Gateway"
	@echo "------------------------------"
	@echo "Usage: make [target]"
	@echo ""
	@echo "Variables:"
	@echo "  TARGET_ARCH          (Optional) Target architecture (e.g., x64, arm64). Default: auto-detected."
	@echo "                       Example: make TARGET_ARCH=arm64 build"
	@echo "  ENV_FILE             (Optional) Path to .env file for docker-compose. Default: ./.env"
	@echo "  COMPOSE_FILE         (Optional) Path to docker-compose file. Default: ./docker-compose.yml"
	@echo ""
	@echo "Main Targets:"
	@echo "  all                  Build all (currently just the service image)."
	@echo "  build                Alias for 'all'."
	@echo "  build-websocket-gateway Build the WebSocket Gateway Docker image."
	@echo ""
	@echo "Unit Testing:"
	@echo "  build-unit-test      Build the Docker image for running unit tests."
	@echo "  test                 Build the unit test image (if not already built) and run tests."
	@echo ""
	@echo "Service Management (Docker Compose):"
	@echo "  start                Start WebSocket Gateway service (and dependencies) in detached mode."
	@echo "  stop                 Stop and remove running services (alias for 'down')."
	@echo "  down                 Stop and remove services, networks, and volumes."
	@echo "  logs                 Tail logs from running services (specifically for websocket-gateway)."
	@echo ""
	@echo "Cleanup:"
	@echo "  clean                Stop services, remove build artifacts, and related Docker images."
	@echo ""
	@echo "Other:"
	@echo "  help                 Show this help message."
	@echo ""