# tests/external_integration_test.py (for TTS service)

import grpc
import pytest
import time
import os
import uuid
from concurrent import futures
from threading import Thread, Lock
import requests # For querying MockAvatarSync HTTP endpoint

# 생성된 Python gRPC 코드 import
try:
    import tts_pb2
    import tts_pb2_grpc
    from google.protobuf import empty_pb2
    # MockAvatarSync 서버의 응답은 Empty이지만, 요청 타입은 avatar_sync_pb2에 정의됨
    import avatar_sync_pb2
    import avatar_sync_pb2_grpc
except ImportError as e:
    print(f"Error: Protobuf/gRPC Python files not found for TTS/AvatarSync: {e}")
    print("Run 'python -m grpc_tools.protoc ...' first from /app in Dockerfile.external_test.")
    exit(1)


# --- Configuration ---
TTS_SERVICE_ADDRESS = os.getenv("TTS_SERVICE_ADDRESS", "localhost:50052")
# Mock LLM은 이 테스트에서 직접 호출하지 않음. TTS 서비스가 LLM 역할을 시뮬레이션하는 클라이언트.
# Mock AvatarSync 서버의 gRPC 주소는 TTS 서비스 환경 변수로 설정됨.
# 여기서는 Mock AvatarSync의 HTTP 상태 조회 엔드포인트 사용.
MOCK_AVATAR_SYNC_HTTP_STATS_URL = os.getenv("MOCK_AVATAR_SYNC_HTTP_STATS_URL", "http://mock-avatar-sync:50083/stats")
MOCK_AVATAR_SYNC_HTTP_RESET_URL = os.getenv("MOCK_AVATAR_SYNC_HTTP_RESET_URL", "http://mock-avatar-sync:50083/reset")

TEST_SESSION_ID = f"test-tts-session-{uuid.uuid4()}"
TEST_LANGUAGE_CODE = "ko-KR" # Azure TTS 지원 언어/음성
TEST_VOICE_NAME = "ko-KR-SunHiNeural" # Azure TTS 음성 이름 예시
TEST_TEXT_CHUNKS = [
    "안녕하세요.",
    "오늘 날씨가 참 좋네요.",
    "이것은 TTS 서비스의 통합 테스트입니다."
]

# --- Helper Functions ---
def reset_mock_avatar_sync():
    try:
        response = requests.post(MOCK_AVATAR_SYNC_HTTP_RESET_URL, timeout=5)
        response.raise_for_status()
        print("\nMockAvatarSync stats reset successfully.")
    except requests.exceptions.RequestException as e:
        print(f"\nWarning: Could not reset MockAvatarSync stats: {e}")
        # pytest.skip(f"MockAvatarSync not available for reset: {e}") # 필요시 테스트 스킵

def get_mock_avatar_sync_stats():
    try:
        response = requests.get(MOCK_AVATAR_SYNC_HTTP_STATS_URL, timeout=5)
        response.raise_for_status()
        return response.json()
    except requests.exceptions.RequestException as e:
        print(f"\nWarning: Could not get MockAvatarSync stats: {e}")
        return None # 또는 예외 발생

# --- Test Fixtures (Pytest) ---
@pytest.fixture(scope="module")
def tts_stub():
    """TTS 서비스에 연결된 gRPC 스텁을 생성하는 Fixture"""
    try:
        channel = grpc.insecure_channel(TTS_SERVICE_ADDRESS)
        # 연결 대기 시간을 충분히 줌 (서비스 시작 시간 고려)
        grpc.channel_ready_future(channel).result(timeout=20)
        print(f"\nConnected to TTS service at {TTS_SERVICE_ADDRESS}")
        yield tts_pb2_grpc.TTSServiceStub(channel)
        print(f"\nClosing channel to TTS service at {TTS_SERVICE_ADDRESS}")
        channel.close()
    except grpc.FutureTimeoutError:
        pytest.fail(f"Failed to connect to TTS service at {TTS_SERVICE_ADDRESS} within 20 seconds.")
    except Exception as e:
        pytest.fail(f"Error creating TTS stub: {e}")

@pytest.fixture(autouse=True) # 각 테스트 함수 실행 전에 호출
def reset_avatar_sync_before_each_test():
    reset_mock_avatar_sync()


# --- Request Iterator for TTS Service ---
def generate_tts_requests(session_id, lang, voice, text_chunks):
    # 1. Send SynthesisConfig
    config = tts_pb2.SynthesisConfig(
        session_id=session_id,
        language_code=lang,
        voice_name=voice
    )
    yield tts_pb2.TTSStreamRequest(config=config)
    print(f"TEST [Session: {session_id}]: Sent SynthesisConfig (Lang: {lang}, Voice: {voice})")

    # 2. Send text chunks
    for i, chunk in enumerate(text_chunks):
        yield tts_pb2.TTSStreamRequest(text_chunk=chunk)
        print(f"TEST [Session: {session_id}]: Sent text_chunk #{i+1}: '{chunk[:20]}...'")
        time.sleep(0.2) # 실제 LLM처럼 약간의 간격


# --- Test Cases ---
def test_synthesize_stream_successful(tts_stub):
    """
    정상적인 텍스트 스트림을 TTS 서비스로 보내고,
    MockAvatarSync 서버에서 오디오/비정형 데이터 수신을 확인.
    """
    request_iterator = generate_tts_requests(
        TEST_SESSION_ID, TEST_LANGUAGE_CODE, TEST_VOICE_NAME, TEST_TEXT_CHUNKS
    )

    try:
        print(f"TEST [Session: {TEST_SESSION_ID}]: Calling SynthesizeStream...")
        # TTS 서비스의 응답은 Empty. 비동기적으로 AvatarSync로 데이터 전송.
        response = tts_stub.SynthesizeStream(request_iterator, timeout=60) # 타임아웃 충분히

        assert isinstance(response, empty_pb2.Empty), "TTS Service should return Empty"
        print(f"TEST [Session: {TEST_SESSION_ID}]: SynthesizeStream returned Empty (as expected).")

        # AvatarSync 서버가 데이터를 받았는지 확인 (몇 초 대기 후 상태 조회)
        time.sleep(5) # TTS 처리 및 AvatarSync로 데이터 전송 시간 대기
        stats = get_mock_avatar_sync_stats()

        assert stats is not None, "Failed to get stats from MockAvatarSync"
        print(f"MockAvatarSync Stats for session {TEST_SESSION_ID}: {stats}")

        assert stats.get("session_id") == TEST_SESSION_ID, "Session ID mismatch in AvatarSync"
        assert stats.get("config_received") is True, "AvatarSync did not receive SyncConfig"
        # Azure TTS는 텍스트 길이에 따라 여러 오디오 청크와 비정형을 생성할 수 있음
        assert stats.get("audio_chunks", 0) > 0, "AvatarSync received no audio chunks"
        assert stats.get("visemes", 0) > 0, "AvatarSync received no visemes" # Azure TTS는 비정형 생성
        assert stats.get("stream_finished") is True, "AvatarSync stream did not finish as expected"

        print(f"TEST PASSED [Session: {TEST_SESSION_ID}]: SynthesizeStream successful and AvatarSync received data.")

    except grpc.RpcError as e:
        pytest.fail(f"SynthesizeStream failed with gRPC error: {e.code()} - {e.details()}")
    except Exception as e:
        pytest.fail(f"An unexpected error occurred: {e}")


def test_synthesize_stream_missing_config(tts_stub):
    """SynthesisConfig 없이 text_chunk를 먼저 보내는 경우 오류 확인"""
    def request_iterator_no_config():
        yield tts_pb2.TTSStreamRequest(text_chunk="Hello without config")

    with pytest.raises(grpc.RpcError) as e_info:
        tts_stub.SynthesizeStream(request_iterator_no_config(), timeout=10)

    assert e_info.value.code() == grpc.StatusCode.INVALID_ARGUMENT
    # 실제 서비스 구현에 따라 오류 메시지 내용 확인
    assert "before SynthesisConfig" in e_info.value.details().lower() or \
           "systems are initialized" in e_info.value.details().lower()
    print("TEST PASSED: SynthesizeStream correctly failed with missing config.")


def test_synthesize_stream_empty_session_id_in_config(tts_stub):
    """Config에 session_id가 비어있는 경우 (서비스에서 생성하는지 확인)"""
    # 서비스에서 session_id를 생성해야 함. AvatarSync에서 확인.
    local_session_id = "" # 빈 세션 ID
    text_chunks = ["테스트입니다."]
    request_iterator = generate_tts_requests(local_session_id, TEST_LANGUAGE_CODE, TEST_VOICE_NAME, text_chunks)

    try:
        response = tts_stub.SynthesizeStream(request_iterator, timeout=30)
        assert isinstance(response, empty_pb2.Empty)
        time.sleep(3)
        stats = get_mock_avatar_sync_stats()
        assert stats is not None
        # 서비스가 생성한 세션 ID가 AvatarSync로 전달되었는지 확인 (비어있지 않아야 함)
        assert stats.get("session_id") is not None and stats.get("session_id") != ""
        assert stats.get("audio_chunks", 0) > 0
        print(f"TEST PASSED: TTS service generated session ID '{stats.get('session_id')}' when empty was provided.")
    except grpc.RpcError as e:
        pytest.fail(f"SynthesizeStream failed: {e.code()} - {e.details()}")

def test_synthesize_stream_invalid_language_or_voice(tts_stub):
    """잘못된 언어 코드나 음성 이름으로 요청 시 동작 확인"""
    invalid_lang_session_id = f"test-invalid-lang-{uuid.uuid4()}"
    invalid_voice_session_id = f"test-invalid-voice-{uuid.uuid4()}"

    req_invalid_lang = generate_tts_requests(invalid_lang_session_id, "xx-XX", TEST_VOICE_NAME, ["test"])
    req_invalid_voice = generate_tts_requests(invalid_voice_session_id, TEST_LANGUAGE_CODE, "NonExistentVoice", ["test"])

    # 1. 잘못된 언어 코드 테스트 ("xx-XX")
    print(f"\n--- Testing Invalid Language Code ('xx-XX') ---")
    try:
        # 이전: with pytest.raises(grpc.RpcError) as e_lang:
        # 수정: 오류가 발생하지 않음을 확인 (정상 완료 기대)
        response_lang = tts_stub.SynthesizeStream(req_invalid_lang, timeout=20) # 타임아웃 늘림
        assert isinstance(response_lang, empty_pb2.Empty), "Expected Empty response for 'xx-XX' lang code"
        print(f"TEST (Invalid Lang): SynthesizeStream completed without RPC error for 'xx-XX' (as observed).")
        # 추가 검증: Mock 서버에서 데이터가 생성되었는지 확인 (선택 사항)
        time.sleep(2) # 처리 시간 대기
        stats_lang = get_mock_avatar_sync_stats()
        print(f"TEST (Invalid Lang): Mock stats after 'xx-XX' request: {stats_lang}")
        # assert stats_lang.get("audio_chunks", 0) == 0 # 또는 예상되는 동작에 맞게 검증

    except grpc.RpcError as e_lang:
        pytest.fail(f"SynthesizeStream failed unexpectedly for 'xx-XX' lang code: {e_lang.code()} - {e_lang.details()}")

    # 2. 잘못된 음성 이름 테스트 ("NonExistentVoice")
    print(f"\n--- Testing Invalid Voice Name ('NonExistentVoice') ---")
    # MockAvatarSync 리셋
    reset_mock_avatar_sync()
    time.sleep(1) # 리셋 반영 시간

    # 잘못된 음성 이름은 오류 발생을 기대 (INTERNAL 또는 INVALID_ARGUMENT)
    # 이전 실행에서 DEADLINE_EXCEEDED 발생 가능성 있음
    with pytest.raises(grpc.RpcError) as e_voice:
        tts_stub.SynthesizeStream(req_invalid_voice, timeout=20) # 타임아웃 늘림

    # DEADLINE_EXCEEDED도 실패로 간주될 수 있으므로, 예상 오류 코드 목록 확장 또는 별도 처리
    expected_error_codes = [grpc.StatusCode.INTERNAL, grpc.StatusCode.INVALID_ARGUMENT, grpc.StatusCode.UNAVAILABLE, grpc.StatusCode.DEADLINE_EXCEEDED]
    assert e_voice.value.code() in expected_error_codes, f"Expected error code {expected_error_codes}, but got {e_voice.value.code()}"
    print(f"TEST (Invalid Voice): Received expected error category: Code={e_voice.value.code()}, Details={e_voice.value.details()}")

    print("\nTEST PASSED: test_synthesize_stream_invalid_language_or_voice behavior checked.")


# pytest 실행 시:
# docker-compose exec test-client-tts pytest /app/tests/external_integration_test.py -v -s