VENV_NAME = ./venv
PYTHON = $(VENV_NAME)/bin/python
PIP = $(VENV_NAME)/bin/pip

install:
	$(PIP) install --upgrade pip
	$(PIP) install -r requirements-dev.txt

run:
	$(PYTHON) -m uvicorn main:app --reload

test:
	PYTHONPATH=$(CURDIR) pytest --import-mode=importlib tests/


black:
	$(PYTHON) -m black app tests

clean:
	find . -type d -name '__pycache__' -exec rm -r {} +
	rm -rf .pytest_cache
	rm -f app/protos/asr_pb2.py app/protos/asr_pb2_grpc.py


grpc_gen:
	python -m grpc_tools.protoc \
		--proto_path=protos \
		--python_out=protos \
		--grpc_python_out=protos \
		protos/asr.proto
	@echo "Fixing import paths in asr_pb2_grpc.py..."
	@unameOut=$$(uname); \
	if [ "$$unameOut" = "Darwin" ]; then \
		sed -i '' 's/^import asr_pb2 /from . import asr_pb2 /' protos/asr_pb2_grpc.py; \
	else \
		sed -i 's/^import asr_pb2 /from . import asr_pb2 /' protos/asr_pb2_grpc.py; \
	fi

grpc_run:
	PYTHONPATH=. python app/main_grpc.py
