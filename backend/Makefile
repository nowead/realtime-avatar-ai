# Makefile for Backend Docker Compose

# === Detect target architecture ===
# 기본값은 uname -m 결과, x86_64→x64, aarch64→arm64
TARGET_ARCH ?= $(shell uname -m)
ifeq ($(TARGET_ARCH),x86_64)
	TARGET_ARCH := x64
else ifeq ($(TARGET_ARCH),aarch64)
	TARGET_ARCH := arm64
else ifeq ($(TARGET_ARCH),arm64)
	TARGET_ARCH := arm64
else
	$(warning Unsupported architecture '$(TARGET_ARCH)'. Falling back to x64.)
	TARGET_ARCH := x64
endif

# === Variables ===
COMPOSE_FILE ?= docker-compose.yml
COMPOSE      := docker compose -f $(COMPOSE_FILE)

# === Phony Targets ===
.PHONY: all build up start restart stop down logs ps help

all: build up

# Build images for all services, passing TARGET_ARCH as build-arg
build:
	@echo "🚀 Building all services (TARGET_ARCH=$(TARGET_ARCH))..."
	@TARGET_ARCH=$(TARGET_ARCH) \
	$(COMPOSE) build --pull --parallel --build-arg TARGET_ARCH=$(TARGET_ARCH)
	@echo "✅ Build complete."

# Start containers in detached mode (rebuild with TARGET_ARCH)
up:
	@echo "🚀 Starting all services in detached mode (TARGET_ARCH=$(TARGET_ARCH))..."
	@TARGET_ARCH=$(TARGET_ARCH) \
	$(COMPOSE) up -d --build
	@echo "✅ Services are up."

start: up

restart:
	@echo "🔄 Restarting all services..."
	@TARGET_ARCH=$(TARGET_ARCH) \
	$(COMPOSE) down
	@TARGET_ARCH=$(TARGET_ARCH) \
	$(COMPOSE) up -d --build
	@echo "✅ Restart complete."

stop:
	@echo "⏹ Stopping and removing all services..."
	@TARGET_ARCH=$(TARGET_ARCH) \
	$(COMPOSE) down -v
	@echo "✅ Services stopped."

down: stop

ps:
	@echo "📋 Current service status:"
	@TARGET_ARCH=$(TARGET_ARCH) \
	$(COMPOSE) ps

logs:
	@echo "📜 Tailing logs for all services..."
	@TARGET_ARCH=$(TARGET_ARCH) \
	$(COMPOSE) logs -f --tail=100

help:
	@echo ""
	@echo "Usage: make [target]"
	@echo ""
	@echo "Targets:"
	@echo "  all       Build images and start all services (default)."
	@echo "  build     Build or rebuild images for all services."
	@echo "  up        Start all services in detached mode (with build)."
	@echo "  start     Alias for up."
	@echo "  restart   Restart all services (down → up)."
	@echo "  stop      Stop and remove containers, networks, volumes."
	@echo "  down      Alias for stop."
	@echo "  ps        List running services and their state."
	@echo "  logs      Tail logs from all services (last 100 lines)."
	@echo "  help      Show this help message."
	@echo ""
