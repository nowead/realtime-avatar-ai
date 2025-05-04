# === Variables ===

# 아키텍처 설정: 기본은 시스템 감지, 사용자 지정 가능
TARGET_ARCH ?= $(shell uname -m)
ifeq ($(TARGET_ARCH),x86_64)
    TARGET_ARCH := x64
endif
ifeq ($(TARGET_ARCH),aarch64)
    TARGET_ARCH := arm64
endif

STT_IMAGE_NAME := stt-service
STT_DOCKERFILE := Dockerfile

UNIT_TEST_IMAGE_NAME := stt-unit-test
UNIT_TEST_DOCKERFILE := Dockerfile.unit_test

EXTERNAL_TEST_IMAGE_NAME := stt-test-external
EXTERNAL_TEST_DOCKERFILE := Dockerfile.test_external

COMPOSE_FILE := docker-compose.yml

# === Targets ===

.PHONY: all build build-stt build-test-client test start stop clean help

all: build

build: build-stt build-external-test
	@echo "✅ All Docker images built successfully."

build-stt:
	@echo "🚀 Building STT Service image (${STT_IMAGE_NAME}) for arch: ${TARGET_ARCH}..."
	docker build \
		--build-arg TARGET_ARCH=${TARGET_ARCH} \
		-t ${STT_IMAGE_NAME} \
		-f ${STT_DOCKERFILE} .

build-unit-test:
	@echo "🧪 Building Unit Test image (${STT_IMAGE_NAME}-unit-test)..."
	docker build \
		--build-arg TARGET_ARCH=${TARGET_ARCH} \
		-t ${UNIT_TEST_IMAGE_NAME} \
		-f ${UNIT_TEST_DOCKERFILE} . --no-cache

build-external-test:
	@echo "🧪 Building External Test Client image (${EXTERNAL_TEST_IMAGE_NAME})..."
	docker build \
		--build-arg TARGET_ARCH=${TARGET_ARCH} \
		-t ${EXTERNAL_TEST_IMAGE_NAME} \
		-f ${EXTERNAL_TEST_DOCKERFILE} . --no-cache

unit-test: build-unit-test
	@echo "🧪 Running Unit Tests..."
	docker run --rm ${UNIT_TEST_IMAGE_NAME}

external-test: build
	@echo "📊 Running integration tests via Docker Compose..."
	TARGET_ARCH=${TARGET_ARCH} docker compose -f ${COMPOSE_FILE} up --build --abort-on-container-exit --exit-code-from stt-service
	@echo "📊 Integration tests completed. Stopping services..."

start:
	@echo "🚀 Starting STT service and Mock LLM service in detached mode..."
	TARGET_ARCH=${TARGET_ARCH} docker compose -f ${COMPOSE_FILE} up -d --build stt-service mock-llm
	@echo "✅ Services started. Use 'make logs' or 'make stop'."

down:
	@echo "🛑 Stopping services defined in ${COMPOSE_FILE}..."
	docker compose -f ${COMPOSE_FILE} down
	@echo "✅ Services stopped."

stop:
	@echo "🛑 Stopping services defined in ${COMPOSE_FILE}..."
	docker compose -f ${COMPOSE_FILE} down

clean: stop
	@echo "🧹 Cleaning up build artifacts..."
	rm -rf build
	@echo "🧹 Cleaning up Docker Compose resources..."
	@echo "✅ Cleanup complete."

logs:
	@echo "📜 Tailing logs for running services..."
	docker compose -f ${COMPOSE_FILE} logs -f --tail=50

help:
	@echo "📜 Makefile Help: Available targets:"
	@echo "  all                - Build all images (default target)"
	@echo "  build              - Build STT service image"
	@echo "  build-stt          - Build STT service image"
	@echo "  build-unit-test    - Build Unit Test image"
	@echo "  build-external-test- Build External Test Client image"
	@echo "  unit-test          - Run unit tests in Docker container"
	@echo "  test               - Run integration tests using Docker Compose"
	@echo "  start              - Start STT service and Mock LLM service"
	@echo "  stop               - Stop running services"
	@echo "  clean              - Clean up build artifacts and stopped containers"
	@echo "  logs               - Tail logs for running services"