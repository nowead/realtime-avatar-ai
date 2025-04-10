FROM python:3.11-slim

# 시스템 패키지 업데이트 및 rust 설치에 필요한 패키지 설치
RUN apt-get update && apt-get install -y \
    curl \
    build-essential \
    git \
    && curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y \
    && . "$HOME/.cargo/env" \
    && export PATH="$HOME/.cargo/bin:$PATH"

# 작업 디렉토리 설정
WORKDIR /app

# requirements 먼저 복사하고 설치 (캐싱 최적화)
COPY requirements.txt requirements.txt
COPY requirements-dev.txt requirements-dev.txt

# Rust 환경 변수 설정 및 requirements 설치
RUN . "$HOME/.cargo/env" && \
    export PATH="$HOME/.cargo/bin:$PATH" && \
    pip install --upgrade pip && \
    pip install -r requirements.txt

# 코드 복사
COPY . .

# 기본 실행 명령
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000"]
