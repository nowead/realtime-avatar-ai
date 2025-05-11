# tests/external_integration_test.py

import grpc
import pytest
import time
import os
from threading import Thread
import logging

# llm_engine/protos/ 에서 생성된 파일 import 필요
try:
    import llm_pb2
    import llm_pb2_grpc
    from google.protobuf import empty_pb2
except ImportError as e:
    print(f"Error: Protobuf/gRPC Python files not found: {e}", file=sys.stderr)
    # PYTHONPATH 확인 등 디버깅 메시지 추가 가능
    exit(1)

logging.basicConfig(level=logging.INFO, format='%(asctime)s - TestClient - %(levelname)s - %(message)s')

# --- Configuration ---
LLM_SERVICE_ADDRESS = os.getenv("LLM_SERVICE_ADDRESS", "localhost:50052")
# MOCK_TTS_ADDRESS = os.getenv("MOCK_TTS_ADDRESS", "localhost:50053") # Mock 서버 상태 조회 시 필요

# --- Test Fixtures (Pytest) ---

@pytest.fixture(scope="module")
def llm_stub():
    """LLM 서비스에 연결된 gRPC 스텁을 생성하는 Fixture"""
    channel = None
    try:
        logging.info(f"Attempting to connect to LLM service at {LLM_SERVICE_ADDRESS}...")
        channel = grpc.insecure_channel(LLM_SERVICE_ADDRESS)
        # 연결 상태 확인 (타임아웃 늘림)
        grpc.channel_ready_future(channel).result(timeout=15)
        logging.info(f"Connected to LLM service at {LLM_SERVICE_ADDRESS}")
        yield llm_pb2_grpc.LLMServiceStub(channel)
    except grpc.FutureTimeoutError:
        pytest.fail(f"Failed to connect to LLM service at {LLM_SERVICE_ADDRESS} within 15 seconds.")
    except Exception as e:
        pytest.fail(f"Error creating LLM stub: {e}")
    finally:
        if channel:
            logging.info(f"Closing channel to LLM service at {LLM_SERVICE_ADDRESS}")
            channel.close()

# --- Helper Functions ---

def create_request_iterator(session_id, text_chunks):
    """gRPC ProcessTextStream 요청 이터레이터를 생성"""
    # 1. Send SessionConfig
    config = llm_pb2.SessionConfig(session_id=session_id)
    yield llm_pb2.LLMStreamRequest(config=config)
    logging.info(f"TEST [{session_id}]: Sent SessionConfig")

    # 2. Send Text Chunks
    sent_chunk_count = 0
    for chunk in text_chunks:
        yield llm_pb2.LLMStreamRequest(text_chunk=chunk)
        sent_chunk_count += 1
        # 서버 처리 시간 고려하여 약간의 지연 추가 가능
        # time.sleep(0.05)
    logging.info(f"TEST [{session_id}]: Finished sending {sent_chunk_count} text chunks")

# --- Test Cases ---

def test_process_text_stream_successful(llm_stub):
    """
    정상적인 텍스트 스트림을 전송하고 LLM 서비스가 성공적으로 응답하는지 검증.
    (Mock TTS 서버 로그 또는 상태를 별도로 확인하여 내용 전달 검증 필요)
    """
    test_session_id = "test-session-123"
    test_data = ["안녕하세요, ", "오늘 날씨는 어떤가요? ", "매우 궁금합니다."]
    request_iterator = create_request_iterator(test_session_id, test_data)

    try:
        logging.info(f"TEST [{test_session_id}]: Calling ProcessTextStream...")
        # 서버의 OpenAI 처리 시간 등을 고려하여 타임아웃 설정
        response = llm_stub.ProcessTextStream(request_iterator, timeout=60) # 예: 60초 타임아웃
        logging.info(f"TEST [{test_session_id}]: ProcessTextStream finished with response type: {type(response)}")

        # LLM 서비스는 Empty를 반환해야 함
        assert isinstance(response, empty_pb2.Empty)

        logging.info("TEST PASSED: ProcessTextStream successful (returned Empty).")
        logging.warning("=> VERIFICATION NEEDED: Check 'mock-tts-server' container logs for received chunks.")
        print("\nVERIFICATION NEEDED: Check 'mock-tts-server' container logs for received chunks for session 'test-session-123'.\n")


    except grpc.RpcError as e:
        pytest.fail(f"ProcessTextStream failed with gRPC error: {e.code()} - {e.details()}")
    except Exception as e:
        pytest.fail(f"An unexpected error occurred: {e}")

def test_process_text_stream_missing_config(llm_stub):
    """설정 요청 없이 바로 텍스트 청크를 보내는 경우 INVALID_ARGUMENT 오류 확인"""
    def request_iterator_no_config():
        yield llm_pb2.LLMStreamRequest(text_chunk="데이터 먼저 보냄")

    with pytest.raises(grpc.RpcError) as e:
        llm_stub.ProcessTextStream(request_iterator_no_config())

    assert e.value.code() == grpc.StatusCode.INVALID_ARGUMENT
    assert "Initial request must be SessionConfig" in e.value.details() # 서버 에러 메시지 확인
    logging.info("TEST PASSED: ProcessTextStream correctly failed when config is missing.")

def test_process_text_stream_missing_session_id(llm_stub):
    """설정 요청 시 session_id가 누락된 경우 INVALID_ARGUMENT 오류 확인"""
    def request_iterator_no_session_id():
        config = llm_pb2.SessionConfig(session_id="") # 빈 세션 ID
        yield llm_pb2.LLMStreamRequest(config=config)

    with pytest.raises(grpc.RpcError) as e:
        llm_stub.ProcessTextStream(request_iterator_no_session_id())

    assert e.value.code() == grpc.StatusCode.INVALID_ARGUMENT
    assert "Session ID is missing" in e.value.details() # 서버 에러 메시지 확인
    logging.info("TEST PASSED: ProcessTextStream correctly failed with missing session_id.")
