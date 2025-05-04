# Makefile for STT Service Project

# === Variables ===

# Docker 이미지 이름 및 태그 설정
STT_IMAGE_NAME := stt-service
STT_DOCKERFILE := Dockerfile

TEST_CLIENT_IMAGE_NAME := stt-test-external
TEST_CLIENT_DOCKERFILE := Dockerfile.test_external

# Docker Compose 파일 이름
COMPOSE_FILE := docker-compose.yml

# === Targets ===

# .PHONY: 가짜 타겟 선언 (파일 이름과 혼동 방지)
.PHONY: all build build-stt build-test-client test start stop clean help

# 기본 타겟: 모든 이미지 빌드
all: build

# 모든 Docker 이미지 빌드
build: build-stt#build-test-client
	@echo "✅ All Docker images built successfully."

# STT 서비스 Docker 이미지 빌드
build-stt:
	@echo "🚀 Building STT Service image (${STT_IMAGE_NAME})..."
	docker build -t ${STT_IMAGE_NAME} -f ${STT_DOCKERFILE} . --no-cache

build-unit-test:
	@echo "🧪 Building Unit Test image (${STT_IMAGE_NAME}-unit-test)..."
	docker build -t ${STT_IMAGE_NAME}-unit-test -f ${STT_DOCKERFILE} . --no-cache

# 테스트 클라이언트 Docker 이미지 빌드
build-test-client:
	@echo "🧪 Building External Test Client image (${TEST_CLIENT_IMAGE_NAME})..."
	docker build -t ${TEST_CLIENT_IMAGE_NAME} -f ${TEST_CLIENT_DOCKERFILE} . -no-cache

unit-test:
	@echo "🧪 Running Unit Tests..."
	# 빌드된 단위 테스트 이미지를 사용하여 테스트 실행
	docker run --rm ${STT_IMAGE_NAME}-unit-test
	@echo "✅ Unit tests completed."

# Docker Compose를 사용하여 통합 테스트 실행
# --build: 필요시 이미지 빌드
# --abort-on-container-exit: test-client 컨테이너 종료 시 다른 컨테이너도 중지하고 종료
# test-client: 실행할 서비스 지정 (의존성 서비스는 자동으로 시작됨)
# 종료 코드가 test-client의 종료 코드를 반영하므로 테스트 성공/실패 확인 가능
test: build # 테스트 전에 빌드를 수행하도록 의존성 추가
	@echo "📊 Running integration tests via Docker Compose..."
	docker-compose -f ${COMPOSE_FILE} up --build --abort-on-container-exit test-client
	@echo "🧹 Cleaning up test containers..."
	# 테스트 후 항상 컨테이너 정리 (오류 발생 시에도 정리되도록)
	docker-compose -f ${COMPOSE_FILE} down

# stt-service와 mock-llm 서비스를 백그라운드(-d)에서 시작
start:
	@echo "🚀 Starting STT service and Mock LLM service in detached mode..."
	docker-compose -f ${COMPOSE_FILE} up -d --build stt-service mock-llm
	@echo "✅ Services started. Use 'make logs' or 'make stop'."

# docker-compose로 실행된 모든 서비스 중지 및 제거
stop:
	@echo "🛑 Stopping services defined in ${COMPOSE_FILE}..."
	docker-compose -f ${COMPOSE_FILE} down

# 빌드 아티팩트 및 Docker 리소스 정리
clean: stop # 서비스를 먼저 중지
	@echo "🧹 Cleaning up build artifacts..."
	# CMake 빌드 디렉토리 삭제 (이름이 'build'라고 가정)
	rm -rf build
	@echo "🧹 Cleaning up Docker Compose resources (stopped containers, networks)..."
	# 'stop' 타겟에서 이미 down이 호출되었음
	# 필요시 볼륨도 삭제: docker-compose -f ${COMPOSE_FILE} down --volumes
	# 필요시 Docker 이미지 삭제 (주의!)
	# docker rmi ${STT_IMAGE_NAME}:${STT_IMAGE_TAG} || true
	# docker rmi ${TEST_CLIENT_IMAGE_NAME}:${TEST_CLIENT_IMAGE_TAG} || true
	@echo "✅ Cleanup complete."

# 로그 확인 (백그라운드 실행 중인 서비스 로그)
logs:
	@echo "📜 Tailing logs for running services..."
	docker-compose -f ${COMPOSE_FILE} logs -f --tail=50

# 도움말 메시지 표시
help:
	@echo "Makefile for STT Service Project"
	@echo ""
	@echo "Available targets:"
	@echo "  make build             Build all necessary Docker images."
	@echo "  make build-stt         Build only the STT service image."
	@echo "  make build-test-client Build only the test client image."
	@echo "  make test              Build images and run integration tests using Docker Compose."
	@echo "  make start             Start STT service and Mock LLM in detached mode."
	@echo "  make stop              Stop and remove services started by 'start' or 'test'."
	@echo "  make clean             Stop services, remove build artifacts and Docker resources."
	@echo "  make logs              Tail logs from running services (started with 'make start')."
	@echo "  make help              Show this help message."
	@echo ""