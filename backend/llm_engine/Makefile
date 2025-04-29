# docker-compose 명령어 기본 설정
COMPOSE = docker compose

# 서비스 이름 (docker-compose.yml 기준)
APP_SERVICE = llm-engine
TEST_SERVICE = test-client

.PHONY: all build up down test logs clean

# 기본 실행: 빌드 후 테스트 실행
all:
	make build

restart:
	$(COMPOSE) restart

build:
	$(COMPOSE) build $(service)

# ✅ 서비스 시작 (백그라운드, --build 옵션 포함)
up:
	$(COMPOSE) up --build

# ✅ 서비스 중지 및 관련 리소스 정리
down:
	$(COMPOSE) down --volumes --remove-orphans --rmi all

# ✅ 통합 테스트 실행 (빌드 후 실행, 테스트 완료 시 자동 종료)
test:
	$(COMPOSE) up --build --exit-code-from $(TEST_SERVICE) --abort-on-container-exit

# ✅ 실행 중인 서비스 로그 확인
logs:
	$(COMPOSE) logs -f $(APP_SERVICE)

# ✅ 생성된 파일 등 정리 (필요 시 추가)
# 예: clean: down
clean: down
# 필요하다면 여기에 로컬 생성 파일 삭제 명령어 추가 (예: rm -rf generated)

# --- 주의: 아래 명령어는 시스템 전체에 영향을 줄 수 있습니다 ---
# ✅ Docker 시스템 전체 정리 (사용 시 주의!)
# prune-all: down
#     docker system prune -af --volumes