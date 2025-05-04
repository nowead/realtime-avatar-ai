# tests/external_integration_test.py

import grpc
import pytest
import time
import os
from concurrent import futures
from threading import Thread

# 생성된 Python gRPC 코드 import
try:
    import stt_pb2
    import stt_pb2_grpc
    # empty_pb2는 테스트 결과 검증에 사용되므로 import 유지
    from google.protobuf import empty_pb2
except ImportError:
    print("Error: Protobuf/gRPC Python files not found.")
    print("Run 'python -m grpc_tools.protoc ...' first.")
    exit(1)


# --- Configuration ---
STT_SERVICE_ADDRESS = os.getenv("STT_SERVICE_ADDRESS", "localhost:50056")
# Mock LLM 주소는 이제 사용하지 않음 (STT 서비스가 내부적으로 연결)
# MOCK_LLM_SERVICE_ADDRESS = os.getenv("MOCK_LLM_SERVICE_ADDRESS", "localhost:50051")
AUDIO_SAMPLE_PATH = os.getenv("AUDIO_SAMPLE_PATH", "tests/sample.wav")
EXPECTED_LANGUAGE = "ko-KR"

# --- Test Fixtures (Pytest) ---

# Mock LLM 서버 fixture 제거됨

@pytest.fixture(scope="module") # Module scope for efficiency
def stt_stub():
    """STT 서비스에 연결된 gRPC 스텁을 생성하는 Fixture"""
    try:
        channel = grpc.insecure_channel(STT_SERVICE_ADDRESS)
        grpc.channel_ready_future(channel).result(timeout=10) # 연결 대기 시간 증가
        print(f"\nConnected to STT service at {STT_SERVICE_ADDRESS}")
        yield stt_pb2_grpc.STTServiceStub(channel) # stub 반환
        print(f"\nClosing channel to STT service at {STT_SERVICE_ADDRESS}")
        channel.close()
    except grpc.FutureTimeoutError:
        pytest.fail(f"Failed to connect to STT service at {STT_SERVICE_ADDRESS} within 10 seconds.")
    except Exception as e:
        pytest.fail(f"Error creating STT stub: {e}")

# --- Helper Functions ---

def generate_audio_chunks(file_path, chunk_size=1024):
    """오디오 파일을 읽어 청크 스트림(bytes)을 생성하는 제너레이터 (이전과 동일)"""
    try:
        with open(file_path, 'rb') as f:
            header = f.read(44)
            if not header or len(header) < 44:
                 print(f"Warning: Could not read WAV header from {file_path}. Assuming raw PCM.")
                 f.seek(0)
            while True:
                chunk = f.read(chunk_size)
                if not chunk:
                    break
                yield chunk
    except FileNotFoundError:
        pytest.fail(f"Audio sample file not found: {file_path}")
    except Exception as e:
        pytest.fail(f"Error reading audio file {file_path}: {e}")

def create_request_iterator(language, audio_file, chunk_size=1024):
    """gRPC RecognizeStream 요청 이터레이터를 생성"""
    config = stt_pb2.RecognitionConfig(language=language)
    yield stt_pb2.STTStreamRequest(config=config)
    print("TEST: Sent RecognitionConfig")
    sent_chunk_count = 0
    for audio_chunk_data in generate_audio_chunks(audio_file, chunk_size):
        # ---===>>> 수정된 부분 시작 <<<===---
        # AudioChunk 메시지를 별도로 만들지 않고,
        # STTStreamRequest의 audio_chunk 필드 (bytes 타입)에 직접 데이터 할당
        yield stt_pb2.STTStreamRequest(audio_chunk=audio_chunk_data)
        # ---===>>> 수정된 부분 끝 <<<===---
        sent_chunk_count += 1
        # 필요시 약간의 지연 추가 (선택 사항)
        # time.sleep(0.01)
    print(f"TEST: Finished sending {sent_chunk_count} audio chunks")


# --- Test Cases ---

def test_recognize_stream_successful(stt_stub):
    """
    정상적인 오디오 스트림을 전송하고 STT 서비스가 성공적으로 응답하는지 검증.
    (Mock LLM 서버 로그를 별도로 확인하여 내용 검증 필요)
    """
    request_iterator = create_request_iterator(EXPECTED_LANGUAGE, AUDIO_SAMPLE_PATH)

    try:
        print("TEST: Calling RecognizeStream...")
        # 서버의 타임아웃 설정 고려하여 클라이언트 타임아웃 설정 가능
        response = stt_stub.RecognizeStream(request_iterator, timeout=60) # 예: 60초 타임아웃
        print(f"TEST: RecognizeStream finished with response: {type(response)}")

        assert isinstance(response, empty_pb2.Empty)

        # 검증 강화:
        # 1. 이 테스트 실행 후 'mock-llm' 컨테이너의 로그를 확인하여
        #    "MockLLM: Stream processing finished..." 로그 및 청크 수 확인.
        # 2. Mock LLM 서버에 상태 조회 API를 추가하고, 여기서 호출하여 확인.
        print("TEST PASSED: RecognizeStream successful (returned Empty).")
        print("=> VERIFICATION NEEDED: Check 'mock-llm' container logs for received chunks.")

    except grpc.RpcError as e:
        pytest.fail(f"RecognizeStream failed with gRPC error: {e.code()} - {e.details()}")
    except Exception as e:
        pytest.fail(f"An unexpected error occurred: {e}")

def test_recognize_stream_missing_language(stt_stub):
    """설정 요청 시 언어 코드가 누락된 경우 INVALID_ARGUMENT 오류 확인 (이전과 동일)"""
    def request_iterator_no_lang():
        config = stt_pb2.RecognitionConfig(language="")
        yield stt_pb2.STTStreamRequest(config=config)

    with pytest.raises(grpc.RpcError) as e:
        stt_stub.RecognizeStream(request_iterator_no_lang())

    assert e.value.code() == grpc.StatusCode.INVALID_ARGUMENT
    assert "Language code is missing" in e.value.details()
    print("TEST PASSED: RecognizeStream correctly failed with missing language.")


# def test_recognize_stream_llm_fails(stt_stub):
#     """
#     STT 서비스가 LLM 엔진으로 데이터를 보내는 중간에 LLM이 오류를 반환하는 시나리오.
#     Mock LLM 서버를 외부에서 제어하는 메커니즘이 필요함 (예: API, 환경 변수)
#     """
#     # 1. Mock LLM 서버가 오류를 발생시키도록 설정 (외부 메커니즘 필요)
#     print("TEST: Scenario requires external setup for Mock LLM to inject error.")
#
#     # 2. 요청 보내기
#     request_iterator = create_request_iterator(EXPECTED_LANGUAGE, AUDIO_SAMPLE_PATH)
#
#     # 3. 오류 확인
#     with pytest.raises(grpc.RpcError) as e:
#          print("TEST: Calling RecognizeStream (expecting LLM error propagation)...")
#          stt_stub.RecognizeStream(request_iterator)
#
#     print(f"TEST: Received gRPC Error: Code={e.value.code()}, Details={e.value.details()}")
#     assert e.value.code() in [grpc.StatusCode.INTERNAL, grpc.StatusCode.UNAVAILABLE] # 예시
#     print("TEST PASSED (structure only): RecognizeStream correctly handled LLM failure.")
#     pytest.skip("Skipping test: Requires external Mock LLM error injection setup.")


# pytest 실행 시 이 파일 실행
# 예: pytest tests/external_integration_test.py -v -s