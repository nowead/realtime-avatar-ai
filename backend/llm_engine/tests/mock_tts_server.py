# tests/mock_tts_server.py

import grpc
from concurrent import futures
import time
import os
import sys
from threading import Lock, Event
import logging

# llm_engine/protos/ 에서 생성된 파일 import 필요
try:
    import tts_pb2
    import tts_pb2_grpc
    # Empty 메시지 타입 import
    from google.protobuf import empty_pb2
except ImportError as e:
    print(f"Error: Failed to import generated protobuf code: {e}", file=sys.stderr)
    # PYTHONPATH 확인 등 디버깅 메시지 추가 가능
    sys.exit(1)

logging.basicConfig(level=logging.INFO, format='%(asctime)s - MockTTS - %(levelname)s - %(message)s')

# 환경 변수에서 포트 설정
MOCK_SERVER_PORT = os.getenv("MOCK_TTS_PORT", "50053")
MOCK_SERVER_ADDRESS = f'[::]:{MOCK_SERVER_PORT}'

# Mock TTS Service 구현
class MockTTSService(tts_pb2_grpc.TTSServiceServicer):
    def __init__(self):
        self._lock = Lock()
        # 세션별 데이터 저장 (테스트 검증용)
        self.sessions = {} # session_id -> {config: ..., chunks: [], finished: False}
        self._stop_event = Event()

    def _clear_session(self, session_id):
        with self._lock:
            if session_id in self.sessions:
                del self.sessions[session_id]
                logging.info(f"Cleared data for session {session_id}")

    def SynthesizeStream(self, request_iterator, context):
        peer = context.peer()
        logging.info(f"Connection received from {peer} for SynthesizeStream.")
        session_id = "unknown"
        session_initialized = False
        current_session_data = None

        try:
            for request in request_iterator:
                request_type = request.WhichOneof('request_data')

                if not session_initialized:
                    if request_type == 'config':
                        config = request.config
                        session_id = config.session_id
                        with self._lock:
                            # 이전 세션 데이터 정리 (선택 사항)
                            # self._clear_session(session_id)
                            self.sessions[session_id] = {
                                'config': config,
                                'chunks': [],
                                'finished': False,
                                'peer': peer
                            }
                            current_session_data = self.sessions[session_id]
                        session_initialized = True
                        logging.info(f"[{session_id}] Received SynthesisConfig: lang={config.language_code}, voice={config.voice_name}")
                    else:
                        logging.error(f"Expected SynthesisConfig first, got {request_type}. Peer: {peer}")
                        context.abort(grpc.StatusCode.INVALID_ARGUMENT, "Expected SynthesisConfig first")
                        return empty_pb2.Empty()
                else: # Session initialized, expect text chunks
                    if request_type == 'text_chunk':
                        chunk = request.text_chunk
                        with self._lock:
                            if current_session_data: # Ensure data structure exists
                                current_session_data['chunks'].append(chunk)
                        # 로그는 줄이기 위해 주석 처리 (필요시 활성화)
                        # logging.info(f"[{session_id}] Received text chunk (len={len(chunk)}): {chunk[:80]}...")
                        # Mock 서버에서 약간의 지연 추가 가능 (테스트 목적)
                        # time.sleep(0.01)
                    elif request_type == 'config':
                        logging.error(f"[{session_id}] Received unexpected SynthesisConfig after initialization.")
                        context.abort(grpc.StatusCode.INVALID_ARGUMENT, "Unexpected SynthesisConfig")
                        return empty_pb2.Empty()
                    else:
                        logging.warning(f"[{session_id}] Received unknown message type '{request_type}'.")

                # 외부에서 중지 신호 확인 (선택 사항)
                if self._stop_event.is_set():
                    logging.warning(f"[{session_id}] Stop event received, aborting stream.")
                    context.abort(grpc.StatusCode.ABORTED, "Server shutting down")
                    return empty_pb2.Empty()

        except grpc.RpcError as e:
             if e.code() == grpc.StatusCode.CANCELLED:
                 logging.warning(f"[{session_id}] Client cancelled the stream. Peer: {peer}")
             else:
                 logging.error(f"[{session_id}] RpcError during stream processing: {e.code()} - {e.details()}. Peer: {peer}")
        except Exception as e:
            logging.exception(f"[{session_id}] Unexpected error receiving stream from {peer}: {e}") # Log stack trace
            context.abort(grpc.StatusCode.INTERNAL, f"MockTTS internal error: {e}")
        finally:
            # 스트림 종료 시 상태 업데이트
            with self._lock:
                if session_id != "unknown" and session_id in self.sessions:
                    self.sessions[session_id]['finished'] = True
                    chunk_count = len(self.sessions[session_id]['chunks'])
                    logging.info(f"[{session_id}] Stream processing finished. Received {chunk_count} text chunks. Peer: {peer}")
                elif session_id == "unknown":
                    logging.warning(f"Stream finished for an unknown or uninitialized session from {peer}.")
                else: # session_id known but not in sessions (e.g., config failed)
                    logging.warning(f"Stream finished for session {session_id} which was not properly initialized or already cleared. Peer: {peer}")


        # 성공적으로 완료되면 Empty 반환
        return empty_pb2.Empty()

    # 테스트 검증을 위한 상태 조회 함수 (선택 사항)
    def get_session_data(self, session_id):
        with self._lock:
            return self.sessions.get(session_id, None)

    def stop(self):
        self._stop_event.set()

def serve():
    """gRPC 서버를 생성하고 실행합니다."""
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    mock_service = MockTTSService()
    tts_pb2_grpc.add_TTSServiceServicer_to_server(mock_service, server)

    server.add_insecure_port(MOCK_SERVER_ADDRESS)
    logging.info(f"Mock TTS Server listening on {MOCK_SERVER_ADDRESS}")
    server.start()

    try:
        while not mock_service._stop_event.wait(timeout=1): # 주기적으로 확인
            pass
    except KeyboardInterrupt:
        logging.info("KeyboardInterrupt received, stopping server...")
    finally:
        mock_service.stop()
        server.stop(grace=1) # 1초 대기 후 종료
        logging.info("Mock TTS server stopped.")

if __name__ == '__main__':
    serve()