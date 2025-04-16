VENV_NAME = .venv
PYTHON = $(VENV_NAME)/bin/python
PIP = $(VENV_NAME)/bin/pip

# Python 가상환경 설치 및 의존성
install:
	$(PIP) install --upgrade pip
	$(PIP) install -r requirements-dev.txt

# FastAPI 앱 실행 (개발용)
run:
	$(PYTHON) -m uvicorn app.main:app --reload

# 테스트 실행
test:
	PYTHONPATH=. pytest tests/

# 포맷 정리 (원하면 black이나 ruff도 추가 가능)
format:
	$(PYTHON) -m black app tests

# Docker 이미지 빌드
build:
	docker build -t llm-engine .

# Docker 컨테이너 실행
docker-run:
	docker run -it --rm -p 8000:8000 llm-engine

# Docker + FastAPI 자동 실행
docker-dev:
	docker run -it --rm -p 8000:8000 -v $$PWD:/app llm-engine uvicorn app.main:app --reload --host 0.0.0.0
