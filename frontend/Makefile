# Makefile

# ==============================================================================
# 변수 정의
# ==============================================================================

# Docker 이미지 이름
IMAGE_NAME := frontend-app
# Docker 이미지 태그
IMAGE_TAG := latest
# 실행될 컨테이너 이름
CONTAINER_NAME := my-frontend-container
# 호스트에서 노출할 포트
HOST_PORT := 8080
# 컨테이너 내부 포트 (Dockerfile에서 EXPOSE한 포트)
CONTAINER_PORT := 80

# Dockerfile 경로 (Makefile과 동일한 디렉토리에 있다고 가정)
DOCKERFILE := ./Dockerfile
# Docker 빌드 컨텍스트 (Makefile과 동일한 디렉토리를 컨텍스트로 사용)
DOCKER_CONTEXT := .

# ==============================================================================
# 타겟 정의
# ==============================================================================

.PHONY: all build run start stop logs ps clean clean-all shell help

all: build run ## Docker 이미지 빌드 및 컨테이너 실행

build: ## Docker 이미지를 빌드합니다.
	@echo "Docker 이미지를 빌드합니다: $(IMAGE_NAME):$(IMAGE_TAG)"
	@docker build -t $(IMAGE_NAME):$(IMAGE_TAG) -f $(DOCKERFILE) $(DOCKER_CONTEXT)

run: stop ## 기존 컨테이너를 중지하고 새 컨테이너를 실행합니다 (백그라운드).
	@echo "$(CONTAINER_NAME) 컨테이너를 실행합니다 (포트: $(HOST_PORT):$(CONTAINER_PORT))..."
	@docker run -d -p $(HOST_PORT):$(CONTAINER_PORT) --name $(CONTAINER_NAME) $(IMAGE_NAME):$(IMAGE_TAG)

start: ## 중지된 컨테이너를 시작합니다.
	@echo "$(CONTAINER_NAME) 컨테이너를 시작합니다..."
	@docker start $(CONTAINER_NAME)

stop: ## 실행 중인 컨테이너를 중지합니다.
	@echo "$(CONTAINER_NAME) 컨테이너를 중지합니다..."
	@docker stop $(CONTAINER_NAME) || true
	@echo "$(CONTAINER_NAME) 컨테이너를 삭제합니다..."
	@docker rm $(CONTAINER_NAME) || true

logs: ## 실행 중인 컨테이너의 로그를 실시간으로 확인합니다.
	@echo "$(CONTAINER_NAME) 컨테이너 로그:"
	@docker logs -f $(CONTAINER_NAME)

ps: ## 현재 Docker 프로세스 상태 (관련 컨테이너)를 확인합니다.
	@echo "실행 중인 '$(IMAGE_NAME)' 관련 컨테이너 목록:"
	@docker ps -a --filter "name=$(CONTAINER_NAME)" --filter "ancestor=$(IMAGE_NAME):$(IMAGE_TAG)"

clean: stop ## 빌드된 Docker 이미지와 중지된 컨테이너를 삭제합니다.
	@echo "Docker 이미지를 삭제합니다: $(IMAGE_NAME):$(IMAGE_TAG)"
	@docker rmi $(IMAGE_NAME):$(IMAGE_TAG) || true

clean-all: stop ## 모든 관련 컨테이너 및 이미지를 정리합니다. (주의해서 사용)
	@echo "모든 '$(CONTAINER_NAME)' 컨테이너를 중지하고 삭제합니다..."
	@docker ps -aq --filter "name=$(CONTAINER_NAME)" | xargs -r docker stop | xargs -r docker rm
	@echo "모든 '$(IMAGE_NAME)' 이미지를 삭제합니다..."
	@docker images -q $(IMAGE_NAME) | xargs -r docker rmi -f
	@echo "사용하지 않는 Docker 볼륨 및 네트워크를 정리합니다 (dangling)..."
	@docker volume prune -f || true
	@docker network prune -f || true

shell: ## 실행 중인 컨테이너 내부에서 쉘을 실행합니다 (디버깅용).
	@echo "$(CONTAINER_NAME) 컨테이너 내부 쉘 실행 (Nginx 기본 경로는 /usr/share/nginx/html 입니다)..."
	@docker exec -it $(CONTAINER_NAME) /bin/sh

help: ## 사용 가능한 모든 make 명령어를 보여줍니다.
	@echo "사용 가능한 명령어:"
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | awk 'BEGIN {FS = ":.*?## "}; {printf "\033[36m%-20s\033[0m %s\n", $$1, $$2}'
