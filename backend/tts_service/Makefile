# --- 변수 정의 ---
MAKE			?= make # Make 명령어 자체를 참조할 때 사용 (재귀적 호출 등)
COMPOSE			= docker compose
COMPOSE_FILE	?= docker-compose.yml
service			?=

# --- 테스트 관련 변수 ---
# 테스트 클라이언트가 생성하는 오디오 파일 경로 (호스트 기준)
TEST_OUTPUT_FILE = ./tests/output_stream.pcm
# ffplay 재생 명령어 및 옵션
# -autoexit: 재생 완료 시 자동 종료
# -nodisp: 비디오 디스플레이 창 표시 안함 (오디오만 재생 시)
# -f s16le: 포맷 (signed 16-bit little-endian)
# -ar 16000: 샘플링 속도 (16kHz)
# -ac 1: 채널 (모노)
PLAY_COMMAND = ffplay -autoexit -nodisp -f s16le -ar 16000 -ac 1

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
	@echo "--- 🧹 컨테이너/네트워크/볼륨 정리 완료 ---"

# --- 테스트 및 재생 ---

test: up ## 테스트 실행 후 결과 오디오 자동 재생
	@echo "--- ▶️ 테스트 클라이언트 실행 ---"
	# 테스트 클라이언트 실행 (이 과정에서 TEST_OUTPUT_FILE 생성 가정)
	$(COMPOSE) -f $(COMPOSE_FILE) run --rm test-client
	@echo "--- ✅ 테스트 클라이언트 완료 ---"
	@# --- 생성된 오디오 파일 재생 시작 ---
	@if [ -f "$(TEST_OUTPUT_FILE)" ]; then \
		echo "--- 🔊 테스트 오디오 재생 시도: $(TEST_OUTPUT_FILE) ---"; \
		if command -v $(firstword $(PLAY_COMMAND)) > /dev/null 2>&1; then \
			$(PLAY_COMMAND) $(TEST_OUTPUT_FILE); \
			echo "--- ✅ 오디오 재생 완료 ---"; \
		else \
			echo ""; \
			echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"; \
			echo "! 재생 오류: 호스트 머신에 'ffplay' 명령어를 찾을 수 없습니다.         !"; \
			echo "! 'sudo apt install ffmpeg' 또는 해당 배포판 명령어로 설치해주세요. !"; \
			echo "! 수동 재생: $(PLAY_COMMAND) $(TEST_OUTPUT_FILE)             !"; \
			echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"; \
			echo ""; \
		fi; \
	else \
		echo ""; \
		echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"; \
		echo "! 재생 오류: 테스트 오디오 파일 '$(TEST_OUTPUT_FILE)'을 찾을 수 없습니다. !"; \
		echo "! test_tts_service.cpp 에서 파일 저장 경로를 확인하세요.            !"; \
		echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"; \
		echo ""; \
	fi

# --- 상태 확인 ---
ps:
	$(COMPOSE) -f $(COMPOSE_FILE) ps

logs:
	$(COMPOSE) -f $(COMPOSE_FILE) logs -f $(SERVICE)

# --- 전체 시스템 정리 ---
prune:
	@echo "--- 🧨 전체 Docker 시스템 정리 ---"
	@read -p "⚠️ 사용하지 않는 모든 Docker 리소스(네트워크, 빌드캐시 등)가 삭제됩니다. 계속할까요? (y/N) " c; \
	if [ "$$c" = "y" ] || [ "$$c" = "Y" ]; then \
		docker system prune -af --volumes; \
		echo "--- ✅ 전체 Docker 시스템 정리 완료 ---"; \
	else \
		echo "⛔ 취소됨"; \
	fi

clean: down
	rm -rf $(TEST_OUTPUT_FILE)
	@echo "--- 🧹 테스트 오디오 파일 삭제 완료 ---"

.PHONY: all build build-tts build-test up restart stop down test ps logs prune