# tests/mock_llm_server.py (수정 완료)

import grpc
from concurrent import futures
import time
import os
import sys
from threading import Lock
# Empty 메시지 타입 임포트
from google.protobuf import empty_pb2 # <<< 수정됨

# 생성된 Python gRPC 코드 import
# 사용자 제공 파일 기준으로 llm_pb2 사용
try:
    import llm_pb2
    import llm_pb2_grpc
except ImportError as e: # <<< 수정됨 (오류 메시지 상세화)
    print(f"Error: Failed to import generated protobuf code: {e}", file=sys.stderr)
    print("Python Path:", sys.path, file=sys.stderr)
    print("Current directory files:", os.listdir('.'), file=sys.stderr)
    sys.exit(1)

# 환경 변수에서 포트 설정
MOCK_SERVER_PORT = os.getenv("MOCK_LLM_PORT", "50051")
MOCK_SERVER_ADDRESS = f'[::]:{MOCK_SERVER_PORT}'

# Mock LLM Service 구현
class MockLLMEngineService(llm_pb2_grpc.LLMServiceServicer): # 사용자 파일 기준 llm_pb2_grpc 사용
    def __init__(self):
        self._lock = Lock()
        # 청크 개수만 카운트하도록 단순화
        self.received_chunks_count = 0 # <<< 수정됨
        self.session_id = None
        self.stream_finished = False
        self.request_count = 0

    # ProcessTextStream 메서드 로직 수정
    def ProcessTextStream(self, request_iterator, context):
        peer = context.peer()
        print(f"MockLLM: Connection received from {peer} for ProcessTextStream.")
        session_initialized = False # config 수신 여부 플래그
        current_session = "unknown"

        # 세션 상태 초기화
        with self._lock:
            self.received_chunks_count = 0 # <<< 수정됨
            self.session_id = None
            self.stream_finished = False
            self.request_count = 0

        try:
            for request in request_iterator:
                with self._lock:
                    self.request_count += 1
                    # oneof 필드 확인하여 요청 타입 구분
                    request_type = request.WhichOneof('request_data') # <<< 수정됨

                    if not session_initialized:
                        # 첫 요청은 config여야 함
                        if request_type == 'config':
                            self.session_id = request.config.session_id # <<< 수정됨
                            current_session = self.session_id
                            session_initialized = True
                            print(f"MockLLM: [{current_session}] Received SessionConfig.")
                        else:
                            # 첫 요청이 config가 아니면 오류
                            print(f"MockLLM: Error: Expected SessionConfig as first message, got {request_type}.", file=sys.stderr)
                            context.abort(grpc.StatusCode.INVALID_ARGUMENT, "Expected SessionConfig first")
                            break # 스트림 처리 중단
                    else:
                        # 세션 초기화 이후에는 text_chunk 예상
                        if request_type == 'text_chunk':
                            self.received_chunks_count += 1 # <<< 수정됨
                            # 필요시 청크 내용 로깅 (주석 처리)
                            # text = request.text_chunk
                            # print(f"MockLLM: [{current_session}] Req#{self.request_count} Received chunk len: {len(text)}")
                        elif request_type == 'config':
                            # 초기화 후 config가 또 들어오면 오류
                            print(f"MockLLM: [{current_session}] Error: Received unexpected SessionConfig after initialization.", file=sys.stderr)
                            context.abort(grpc.StatusCode.INVALID_ARGUMENT, "Unexpected SessionConfig")
                            break # 스트림 처리 중단
                        else:
                            # 예상치 못한 타입 경고
                            print(f"MockLLM: [{current_session}] Warning: Received unknown message type '{request_type}' in stream.", file=sys.stderr)

                    # 오류 주입 로직 (주석 유지)
                    # if self.error_to_inject ...

        except grpc.RpcError as e:
             # 클라이언트 연결 종료 등 gRPC 오류 처리
             print(f"MockLLM: [{current_session}] RpcError during stream processing: {e.code()} - {e.details()}", file=sys.stderr)
        except Exception as e:
            # 코드 로직 오류 등 예상 못한 예외 처리
            print(f"MockLLM: [{current_session}] Unexpected error receiving stream: {e}", file=sys.stderr)
            context.abort(grpc.StatusCode.INTERNAL, f"MockLLM internal error: {e}")
            # 오류 발생 시에도 Empty 반환 시도
            return empty_pb2.Empty() # <<< 수정됨

        # 스트림 정상 종료 (클라이언트가 스트림 닫음)
        with self._lock:
            print(f"MockLLM: [{current_session}] Stream processing finished. Received {self.received_chunks_count} text chunks.") # <<< 수정됨 (로그 메시지)
            self.stream_finished = True

        # 성공 시 Empty 반환
        return empty_pb2.Empty() # <<< 수정됨

    # 상태 확인/제어 함수 (주석 유지)
    # def GetState(...)
    # def ResetState(...)

def serve():
    """gRPC 서버를 생성하고 실행합니다."""
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    mock_service = MockLLMEngineService()
    # 사용자 파일 기준 llm_pb2_grpc 사용
    llm_pb2_grpc.add_LLMServiceServicer_to_server(mock_service, server)

    server.add_insecure_port(MOCK_SERVER_ADDRESS)
    print(f"Mock LLM Server listening on {MOCK_SERVER_ADDRESS}")
    server.start()

    try:
        server.wait_for_termination()
    except KeyboardInterrupt:
        print("Stopping Mock LLM server...")
        server.stop(grace=1)
        print("Mock LLM server stopped.")

# 시작 시 디버깅 정보 추가
if __name__ == '__main__':
    # <<< 수정됨 (디버깅 블록 추가) ---
    print("--- MockLLM Debug Info ---")
    print("Current Working Directory:", os.getcwd())
    print("Python Path:", sys.path)
    print("Checking for generated files in /app:")
    try:
        app_files = os.listdir('/app')
        print("/app files:", app_files)
        # import 하는 파일 이름으로 확인
        if 'llm_pb2.py' not in app_files:
             print("WARNING: llm_pb2.py not found in /app", file=sys.stderr)
        if 'llm_pb2_grpc.py' not in app_files:
             print("WARNING: llm_pb2_grpc.py not found in /app", file=sys.stderr)
    except FileNotFoundError:
         print("ERROR: /app directory not found.", file=sys.stderr)
    except Exception as e:
         print(f"ERROR checking /app contents: {e}", file=sys.stderr)
    print("--- Starting Server ---")
    # --- 디버깅 블록 끝 ---
    serve()