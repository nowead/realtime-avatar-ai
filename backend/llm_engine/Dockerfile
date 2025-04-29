FROM python:3.11-slim

WORKDIR /app

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    wget \
    && rm -rf /var/lib/apt/lists/*

# ✅ 필요한 파일 복사
COPY ./protos ./protos
COPY ./src ./src
# requirements 먼저 복사하고 설치 (캐싱 최적화)
COPY requirements.txt requirements.txt
COPY requirements-dev.txt requirements-dev.txt

# ✅ Python 패키지 설치
RUN pip install --no-cache-dir -r requirements.txt

# Dockerfile 예시 (RUN 명령어 추가 위치는 apt-get install 이후 등 적절한 곳)
RUN wget -qO/usr/local/bin/grpc_health_probe https://github.com/grpc-ecosystem/grpc-health-probe/releases/download/v0.4.25/grpc_health_probe-linux-amd64 && \
    chmod +x /usr/local/bin/grpc_health_probe

# ✅ protoc 컴파일 (빌드 시점에)
RUN mkdir generated && \
    touch generated/__init__.py && \
    python -m grpc_tools.protoc -I=protos --python_out=generated --grpc_python_out=generated protos/*.proto
# RUN test -s generated/llm_pb2.py && test -s generated/llm_pb2_grpc.py && echo "Protoc generated files verified."

# ✅ 환경변수 설정
ENV PYTHONUNBUFFERED=1

EXPOSE 50051

CMD ["python", "src/llm_server.py"]
