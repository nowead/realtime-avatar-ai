# Makefile for managing Docker Compose services

# 기본 Docker Compose 파일 지정
COMPOSE_FILE=docker-compose.yml
# .env 파일 로드 (docker-compose가 자동으로 로드하지만 명시적으로 포함 가능)
# include .env
# export $(shell sed 's/=.*//' .env)

# 기본 명령어: 도움말 표시
help:
	@echo "Usage: make [command]"
	@echo ""
	@echo "Commands:"
	@echo "  build service=<service_name>    Build a specific service image"
	@echo "  build-all                       Build all service images defined in docker-compose.yml"
	@echo "  up service=<service_name>       Build and start a specific service in detached mode"
	@echo "  up-all                          Build and start all services in detached mode"
	@echo "  start service=<service_name>    Start an existing service container"
	@echo "  start-all                       Start all existing service containers"
	@echo "  stop service=<service_name>     Stop a running service container"
	@echo "  stop-all                        Stop all running service containers"
	@echo "  down                            Stop and remove all containers, networks"
	@echo "  logs service=<service_name>     Follow logs for a specific service"
	@echo "  logs-all                        Follow logs for all services"
	@echo "  ps                              List running containers for this project"
	@echo "  shell service=<service_name>    Start a shell inside a running service container"
	@echo "  clean                           Remove stopped containers and dangling images/volumes"
	@echo ""
	@echo "Example: make build service=nlu-service"
	@echo "Example: make up service=nlu-service"
	@echo "Example: make logs service=nlu-service"
	@echo "Example: make down"

# 서비스 이름 변수 처리 (기본값 없음)
service = ""

# 서비스별 빌드: make build service=my-service
build:
	@if [ -z "$(service)" ]; then \
		echo "Please specify the service name: make build service=<service_name>"; \
		exit 1; \
	fi
	docker-compose -f $(COMPOSE_FILE) build $(service)

# 전체 서비스 빌드
build-all:
	docker-compose -f $(COMPOSE_FILE) build

# 서비스별 빌드 및 시작 (백그라운드): make up service=my-service
up:
	@if [ -z "$(service)" ]; then \
		echo "Please specify the service name: make up service=<service_name>"; \
		exit 1; \
	fi
	docker-compose -f $(COMPOSE_FILE) up -d --build $(service)

# 전체 서비스 빌드 및 시작 (백그라운드)
up-all:
	docker-compose -f $(COMPOSE_FILE) up -d --build

# 기존 서비스 컨테이너 시작: make start service=my-service
start:
	@if [ -z "$(service)" ]; then \
		echo "Please specify the service name: make start service=<service_name>"; \
		exit 1; \
	fi
	docker-compose -f $(COMPOSE_FILE) start $(service)

# 전체 기존 서비스 컨테이너 시작
start-all:
	docker-compose -f $(COMPOSE_FILE) start

# 실행 중인 서비스 중지: make stop service=my-service
stop:
	@if [ -z "$(service)" ]; then \
		echo "Please specify the service name: make stop service=<service_name>"; \
		exit 1; \
	fi
	docker-compose -f $(COMPOSE_FILE) stop $(service)

# 전체 실행 중인 서비스 중지
stop-all:
	docker-compose -f $(COMPOSE_FILE) stop

# 모든 컨테이너 중지 및 제거
down:
	docker-compose -f $(COMPOSE_FILE) down

# 서비스별 로그 확인 (실시간): make logs service=my-service
logs:
	@if [ -z "$(service)" ]; then \
		echo "Please specify the service name: make logs service=<service_name>"; \
		exit 1; \
	fi
	docker-compose -f $(COMPOSE_FILE) logs -f $(service)

# 전체 서비스 로그 확인 (실시간)
logs-all:
	docker-compose -f $(COMPOSE_FILE) logs -f

# 실행 중인 컨테이너 목록
ps:
	docker-compose -f $(COMPOSE_FILE) ps

# 서비스 컨테이너 내부 쉘 실행: make shell service=my-service
shell:
	@if [ -z "$(service)" ]; then \
		echo "Please specify the service name: make shell service=<service_name>"; \
		exit 1; \
	fi
	docker-compose -f $(COMPOSE_FILE) exec $(service) /bin/sh || docker-compose -f $(COMPOSE_FILE) exec $(service) /bin/bash

# 불필요한 리소스 정리
clean:
	docker-compose -f $(COMPOSE_FILE) down --remove-orphans
	docker volume prune -f
	docker image prune -f

# .PHONY: phony 타겟 선언 (파일 이름과 혼동 방지)
.PHONY: help build build-all up up-all start start-all stop stop-all down logs logs-all ps shell clean