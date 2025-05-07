# Makefile for tts_service

# === Variables ===

# 아키텍처 설정: 기본은 시스템 감지, 사용자 지정 가능 (예: make TARGET_ARCH=x64)
# uname -m 결과에 따라 x64 또는 arm64로 설정
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
TTS_IMAGE_NAME := tts-service
TTS_DOCKERFILE := Dockerfile  # 메인 TTS 서비스 Dockerfile

# 유닛 테스트용 Dockerfile은 별도로 정의하지 않음 (메인 Dockerfile에서 BUILD_TESTING=ON으로 빌드)
# 필요하다면 Dockerfile.unit_test 와 같이 별도 정의 가능
UNIT_TEST_IMAGE_NAME := tts-unit-test # 유닛 테스트용 Docker 이미지 이름
UNIT_TEST_DOCKERFILE := Dockerfile.unit_test # 유닛 테스트용 Dockerfile

# 외부 통합 테스트용 이미지 및 Dockerfile (Python 테스트 환경)
EXTERNAL_TEST_IMAGE_NAME := tts-test-external
EXTERNAL_TEST_DOCKERFILE := Dockerfile.external_test

# Docker Compose 파일
COMPOSE_FILE := docker-compose.yml

# .env 파일 (API 키 및 리전 정보 포함)
ENV_FILE := .env

# === Targets ===

.PHONY: all build build-tts build-external-test unit-test external-test start stop down clean logs help

all: build
	@echo "✅ Default target 'all' (build) completed."

# 메인 서비스 및 테스트 클라이언트 이미지 빌드
build: build-tts #build-external-test
	@echo "✅ All required Docker images built successfully."

# TTS 서비스 Docker 이미지 빌드
build-tts:
	@echo "🚀 Building TTS Service image (${TTS_IMAGE_NAME}) for TARGET_ARCH: ${TARGET_ARCH}..."
	docker build \
		--build-arg TARGET_ARCH=${TARGET_ARCH} \
		-t ${TTS_IMAGE_NAME} \
		-f ${TTS_DOCKERFILE} . --no-cache

build-unit-test:
	@echo "🧪 Building Unit Test image (${TTS_IMAGE_NAME}-unit-test) for TARGET_ARCH: ${TARGET_ARCH}..."
	docker build \
		--build-arg TARGET_ARCH=${TARGET_ARCH} \
		-t ${UNIT_TEST_IMAGE_NAME} \
		-f ${UNIT_TEST_DOCKERFILE} . --no-cache

# 외부 통합 테스트용 Python 클라이언트 이미지 빌드
build-external-test:
	@echo "🧪 Building External Test Client image (${EXTERNAL_TEST_IMAGE_NAME}) for TARGET_ARCH: ${TARGET_ARCH}..."
	docker build \
		--build-arg TARGET_ARCH=${TARGET_ARCH} \
		-t ${EXTERNAL_TEST_IMAGE_NAME}:latest \
		-f ${EXTERNAL_TEST_DOCKERFILE} . --no-cache

# C++ 유닛 테스트 실행 (Docker 내부에서 빌드 및 실행)
# 이 방식은 Dockerfile 내에서 테스트를 빌드하고 실행하는 단계를 포함해야 함.
# 또는, 로컬에서 빌드 후 실행 파일을 컨테이너에 복사하여 실행.
# 여기서는 Dockerfile 내에서 BUILD_TESTING=ON으로 빌드하고, 해당 컨테이너를 실행하여 테스트.
unit-test: build-unit-test
	@echo "🧪 Running Unit Tests inside the container..."
	# unit_tests 실행 파일이 /app/build/unit_tests 에 생성된다고 가정 (CMakeLists.txt 설정에 따라 다름)
	docker run --rm ${UNIT_TEST_IMAGE_NAME}
	@echo "🧪 Unit Tests completed. Check the output above for results."

# 외부 통합 테스트 실행 (Docker Compose 사용)
# test-client-tts 서비스가 테스트를 실행하고 결과를 반환
external-test: build-tts build-external-test
	@echo "📊 Running integration tests via Docker Compose (TARGET_ARCH: ${TARGET_ARCH})..."
	@echo "Ensure your .env file is configured with AZURE_SPEECH_KEY and AZURE_SPEECH_REGION."
	# TARGET_ARCH를 docker-compose up 명령어에 환경 변수로 전달
	TARGET_ARCH=${TARGET_ARCH} docker compose -f ${COMPOSE_FILE} --env-file ${ENV_FILE} up \
		--build \
		--abort-on-container-exit \
		--exit-code-from test-client-tts
	@echo "📊 Integration tests completed. Use 'make down' or 'make logs' for details."

# 별칭: test -> external-test (stt_service Makefile 참고)
test: external-test

# 서비스 시작 (detached mode)
start:
	@echo "🚀 Starting TTS service and Mock AvatarSync service in detached mode (TARGET_ARCH: ${TARGET_ARCH})..."
	TARGET_ARCH=${TARGET_ARCH} docker compose -f ${COMPOSE_FILE} --env-file ${ENV_FILE} up -d --build tts-service mock-avatar-sync
	@echo "✅ Services started. Use 'make logs' or 'make stop'."

# 서비스 중지 및 컨테이너 제거
down:
	@echo "🛑 Stopping and removing services defined in ${COMPOSE_FILE}..."
	TARGET_ARCH=${TARGET_ARCH} docker compose -f ${COMPOSE_FILE} --env-file ${ENV_FILE} down -v --remove-orphans
	@echo "✅ Services stopped and removed."

# 서비스 중지 (down의 별칭)
stop: down

# 빌드 아티팩트 및 Docker 리소스 정리
clean:
	@echo "🧹 Stopping and removing services (if any)..."
	-TARGET_ARCH=${TARGET_ARCH} docker compose -f ${COMPOSE_FILE} --env-file ${ENV_FILE} down -v --remove-orphans
	@echo "🧹 Cleaning up local build artifacts (./build, ./build_unit_test, ./test_results)..."
	rm -rf build
	rm -rf ${UNIT_TEST_BUILD_DIR}
	rm -rf test_results
	@echo "🧹 Removing Docker images (tts-service, tts-test-external)..."
	-docker rmi ${TTS_IMAGE_NAME}:latest ${TTS_IMAGE_NAME}:unit-test ${EXTERNAL_TEST_IMAGE_NAME}:latest
	@echo "🧹 Pruning unused Docker data (optional, be careful)..."
	# -docker system prune -af # 주석 처리: 사용 시 주의
	@echo "✅ Cleanup attempt complete."

# 실행 중인 서비스 로그 확인
logs:
	@echo "📜 Tailing logs for services defined in ${COMPOSE_FILE}..."
	TARGET_ARCH=${TARGET_ARCH} docker compose -f ${COMPOSE_FILE} --env-file ${ENV_FILE} logs -f --tail=100

# 도움말 메시지
help:
	@echo ""
	@echo "Makefile for TTS Service"
	@echo "--------------------------"
	@echo "Usage: make [target]"
	@echo ""
	@echo "Variables:"
	@echo "  TARGET_ARCH      (Optional) Target architecture (e.g., x64, arm64). Default: auto-detected."
	@echo "                     Example: make TARGET_ARCH=arm64 build"
	@echo "  ENV_FILE         (Optional) Path to .env file. Default: ./.env"
	@echo ""
	@echo "Main Targets:"
	@echo "  all                Build all required Docker images (default)."
	@echo "  build              Alias for 'all'."
	@echo "  build-tts          Build the main TTS service Docker image."
	@echo "  build-external-test Build the Python external test client image."
	@echo "  unit-test          Build (if needed) and run C++ unit tests in a Docker container."
	@echo "  external-test      Run Python integration tests using Docker Compose."
	@echo "  test               Alias for 'external-test'."
	@echo ""
	@echo "Service Management (Docker Compose):"
	@echo "  start              Start TTS service and mock-avatar-sync in detached mode."
	@echo "  stop               Stop and remove running services (alias for 'down')."
	@echo "  down               Stop and remove services, networks, and volumes."
	@echo "  logs               Tail logs from running services."
	@echo ""
	@echo "Cleanup:"
	@echo "  clean              Stop services, remove build artifacts, and Docker images."
	@echo ""
	@echo "Other:"
	@echo "  help               Show this help message."
	@echo ""