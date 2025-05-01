# --- 변수 정의 ---
COMPOSE       = docker compose
COMPOSE_FILE ?= docker-compose.yml
SERVICE      ?=

# --- 빌드 ---
all: build

# 모든 서비스 빌드
build: build-tts build-test
	@echo "--- ✅ 모든 서비스 빌드 완료 ---"

# tts-service만 빌드
build-tts:
	@echo "--- 🔨 TTS 서비스 빌드 ---"
	$(COMPOSE) -f $(COMPOSE_FILE) build tts-service
	@echo "--- ✅ TTS 빌드 완료 ---"

# test-client만 빌드
build-test:
	@echo "--- 🔨 테스트 클라이언트 빌드 ---"
	$(COMPOSE) -f $(COMPOSE_FILE) build --no-cache test-client
	@echo "--- ✅ 테스트 클라이언트 빌드 완료 ---"

# --- 실행/중지 ---

up:
	@echo "--- 🚀 서비스 시작 중 ---"
	$(COMPOSE) -f $(COMPOSE_FILE) up -d --remove-orphans
	@echo "--- ✅ 서비스 시작 완료 ---"

restart:
	@echo "--- 🔁 서비스 재시작 ---"
	$(COMPOSE) -f $(COMPOSE_FILE) restart $(SERVICE)
	@echo "--- ✅ 재시작 완료 ---"

stop:
	@echo "--- ⛔ 서비스 중지 ---"
	$(COMPOSE) -f $(COMPOSE_FILE) stop $(SERVICE)
	@echo "--- ✅ 중지 완료 ---"

# --- 완전 삭제 ---

down:
	@echo "--- 💣 모든 Docker Compose 리소스 삭제 중 ---"
	$(COMPOSE) -f $(COMPOSE_FILE) down --volumes --remove-orphans --rmi all
	docker system prune -af --volumes --filter "label=stage=tts-service" --filter "label=stage=test-client" || true
	@echo "--- 🧹 이미지/볼륨/캐시 정리 완료 ---"

# --- 테스트 ---

test:
	$(MAKE) up
	$(COMPOSE) run --rm test-client
	$(MAKE) down

# --- 상태 확인 ---
ps:
	$(COMPOSE) -f $(COMPOSE_FILE) ps

logs:
	$(COMPOSE) -f $(COMPOSE_FILE) logs -f $(SERVICE)

# --- 전체 시스템 정리 ---
prune:
	@echo "--- 🧨 전체 Docker 시스템 정리 ---"
	@read -p "⚠️ 사용하지 않는 모든 Docker 리소스가 삭제됩니다. 계속할까요? (y/N) " c; \
	if [ "$$c" = "y" ] || [ "$$c" = "Y" ]; then \
		docker system prune -af --volumes; \
	else \
		echo "⛔ 취소됨"; \
	fi

.PHONY: all build build-tts build-test up restart stop down test ps logs prune
