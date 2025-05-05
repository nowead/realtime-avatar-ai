# llm_engine/Makefile

# === Variables ===

# 아키텍처 설정: 기본은 시스템 감지, 사용자 지정 가능
TARGET_ARCH ?= $(shell uname -m)
ifeq ($(TARGET_ARCH),x86_64)
    TARGET_ARCH := amd64 # Docker Hub 및 일반적인 표기법에 맞게 변경 (x64 대신 amd64)
endif
ifeq ($(TARGET_ARCH),aarch64)
    TARGET_ARCH := arm64
endif

# --- llm_engine 서비스 이름 및 파일 경로 ---
LLM_IMAGE_NAME := llm-engine-service # docker-compose.yml의 서비스 이름과 유사하게
LLM_DOCKERFILE := Dockerfile

UNIT_TEST_IMAGE_NAME := llm-unit-test
UNIT_TEST_DOCKERFILE := Dockerfile.unit_test

EXTERNAL_TEST_IMAGE_NAME := llm-test-external # Mock TTS 및 Test Client 빌드용 이미지
EXTERNAL_TEST_DOCKERFILE := Dockerfile.test_external

COMPOSE_FILE := docker-compose.yml

# === Targets ===

.PHONY: all build build-llm build-unit-test build-external-test unit-test external-test start stop clean logs help

all: build
	@echo "✅ Default target 'all' executed (equivalent to 'build')."

# 메인 서비스와 외부 테스트 이미지를 빌드합니다.
build: build-llm build-external-test
	@echo "✅ LLM Engine and External Test Docker images built successfully."

# llm-engine 메인 서비스 이미지를 빌드합니다.
build-llm:
	@echo "🚀 Building LLM Engine Service image (${LLM_IMAGE_NAME}) for arch: ${TARGET_ARCH}..."
	docker build \
		--build-arg TARGET_ARCH=${TARGET_ARCH} \
		-t ${LLM_IMAGE_NAME} \
		-f ${LLM_DOCKERFILE} .

# C++ 유닛 테스트 이미지를 빌드합니다.
build-unit-test:
	@echo "🧪 Building Unit Test image (${UNIT_TEST_IMAGE_NAME})..."
	docker build \
		--build-arg TARGET_ARCH=${TARGET_ARCH} \
		-t ${UNIT_TEST_IMAGE_NAME} \
		-f ${UNIT_TEST_DOCKERFILE} . --no-cache

# Python 외부 통합 테스트 및 Mock TTS 서버 이미지를 빌드합니다.
build-external-test:
	@echo "🐍 Building External Test (Python Env) image (${EXTERNAL_TEST_IMAGE_NAME})..."
	# Python 이미지는 아키텍처 인자가 필수는 아닐 수 있음
	docker build \
		--build-arg TARGET_ARCH=${TARGET_ARCH} \
		-t ${EXTERNAL_TEST_IMAGE_NAME} \
		-f ${EXTERNAL_TEST_DOCKERFILE} . --no-cache

# 유닛 테스트를 빌드하고 실행합니다.
unit-test: build-unit-test
	@echo "🧪 Running Unit Tests in Docker container (${UNIT_TEST_IMAGE_NAME})..."
	# Dockerfile.unit_test 내에서 테스트가 실행되도록 설정됨
	docker run --rm ${UNIT_TEST_IMAGE_NAME}
	@echo "✅ Unit tests finished."

# 외부 통합 테스트를 빌드하고 실행합니다. (docker-compose 사용)
external-test: build # 메인 및 외부 테스트 이미지가 필요
	@echo "📊 Running integration tests via Docker Compose..."
	# docker-compose.yml에 정의된 서비스를 빌드하고 실행합니다.
	# --abort-on-container-exit: 컨테이너 하나라도 종료되면 모두 중지
	# --exit-code-from test-client: test-client 컨테이너의 종료 코드를 반환 (테스트 결과)
	TARGET_ARCH=${TARGET_ARCH} docker compose -f ${COMPOSE_FILE} up --build --abort-on-container-exit --exit-code-from test-client
	@echo "📊 Integration tests completed. Stopping services..."
	# 테스트 후 자동으로 서비스 중지 및 제거 (down)
	# make stop # 명시적으로 stop/down 호출 필요 없이 up이 종료되면 됨

# 백그라운드에서 llm-engine과 mock-tts 서비스를 시작합니다.
start: build # 이미지가 필요할 수 있으므로 build 의존성 추가
	@echo "🚀 Starting LLM service and Mock TTS service in detached mode..."
	TARGET_ARCH=${TARGET_ARCH} docker compose -f ${COMPOSE_FILE} up -d --build llm-engine mock-tts
	@echo "✅ Services started. Use 'make logs' or 'make stop'."

# docker-compose로 실행 중인 서비스를 중지하고 컨테이너를 제거합니다.
down: stop

# docker-compose로 실행 중인 서비스를 중지하고 컨테이너를 제거합니다.
stop:
	@echo "🛑 Stopping services defined in ${COMPOSE_FILE}..."
	docker compose -f ${COMPOSE_FILE} down --remove-orphans
	@echo "✅ Services stopped and containers removed."

# 빌드 아티팩트 및 중지된 컨테이너/네트워크 등을 정리합니다.
clean: stop
	@echo "🧹 Cleaning up C++ build artifacts..."
	rm -rf build # CMake 빌드 디렉토리 삭제
	@echo "🧹 Cleaning up Docker Compose resources (volumes, networks - if any unused)..."
	# docker system prune -a # 주의: 사용하지 않는 모든 도커 리소스 삭제!
	@echo "✅ Cleanup potentially complete (manual pruning might be needed)."

# 실행 중인 서비스의 로그를 실시간으로 확인합니다.
logs:
	@echo "📜 Tailing logs for running services..."
	docker compose -f ${COMPOSE_FILE} logs -f --tail=50

# 사용 가능한 Makefile 타겟 목록을 보여줍니다.
help:
	@echo "📜 Makefile Help: Available targets for llm_engine:"
	@echo "  all                  - Build all necessary images (llm-engine, external-test) (default target)"
	@echo "  build                - Build llm-engine and external-test images"
	@echo "  build-llm            - Build the main LLM Engine service image"
	@echo "  build-unit-test      - Build the C++ Unit Test image"
	@echo "  build-external-test  - Build the Python External Test environment image"
	@echo "  unit-test            - Build and run C++ unit tests in Docker"
	@echo "  external-test        - Build images and run Python integration tests using Docker Compose"
	@echo "  start                - Start llm-engine and mock-tts services in detached mode"
	@echo "  stop                 - Stop and remove running services defined in docker-compose.yml"
	@echo "  down                 - Alias for stop"
	@echo "  clean                - Run 'stop' and remove local build directory"
	@echo "  logs                 - Tail logs for running services"
	@echo "  help                 - Show this help message"