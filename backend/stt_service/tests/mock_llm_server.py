# tests/mock_llm_server.py

import grpc
from concurrent import futures
import time
import os
import sys
from threading import Lock

# 생성된 Python gRPC 코드 import (위치에 맞게 조정)
# PYTHONPATH 설정 또는 같은 디렉토리에 생성된 파일 필요
try:
    import llm_engine_pb2
    import llm_engine_pb2_grpc
except ImportError:
    print("Error: llm_engine_pb2* Python files not found.")
    print("Run 'python -m grpc_tools.protoc ...' for llm_engine.proto first.")
    # PYTHONPATH 확인 또는 파일 위치 확인
    sys.exit(1)

# 환경 변수에서 포트 설정 (기본값 50051)
MOCK_SERVER_PORT = os.getenv("MOCK_LLM_PORT", "50051")
MOCK_SERVER_ADDRESS = f'[::]:{MOCK_SERVER_PORT}'

# Mock LLM Service 구현 (external_integration_test.py에서 가져옴)
class MockLLMEngineService(llm_engine_pb2_grpc.LLMServiceServicer):
    def __init__(self):
        self._lock = Lock() # 스레드 안전성을 위한 잠금
        self.received_chunks = []
        self.session_id = None
        self.stream_finished = False
        self.error_to_inject = None # TODO: 외부에서 설정하는 메커니즘 필요 시 추가
        self.request_count = 0

    def ProcessTextStream(self, request_iterator, context):
        peer = context.peer()
        print(f"MockLLM: Connection received from {peer} for ProcessTextStream.")
        # 세션별 상태 초기화 (또는 reset 메소드 사용)
        with self._lock:
            self.received_chunks = []
            self.session_id = None
            self.stream_finished = False
            self.request_count = 0

        current_session = "unknown" # 세션 ID 얻기 전

        try:
            for request in request_iterator:
                with self._lock:
                    self.request_count += 1
                    if not self.session_id:
                        self.session_id = request.session_id
                        current_session = self.session_id
                        print(f"MockLLM: [{current_session}] Received first chunk.")

                    # 받은 청크 데이터 저장 (출력은 부하가 될 수 있으므로 최소화)
                    self.received_chunks.append({
                        "text_len": len(request.text_chunk),
                        "is_final": request.is_final,
                        "req_num": self.request_count
                    })
                    # 상세 로깅 필요 시 아래 주석 해제
                    # print(f"MockLLM: [{current_session}] Req#{self.request_count} Received chunk (final={request.is_final}): '{request.text_chunk[:30]}...'")


                    # TODO: 오류 주입 로직 (환경 변수 또는 다른 메커니즘 사용)
                    # if self.error_to_inject and self.request_count >= error_injection_threshold:
                    #     print(f"MockLLM: Injecting error: {self.error_to_inject.details()}")
                    #     context.abort(self.error_to_inject.code(), self.error_to_inject.details())
                    #     break
        except Exception as e:
            print(f"MockLLM: [{current_session}] Error receiving stream: {e}", file=sys.stderr)
            # 오류 발생 시에도 종료 메시지 반환 시도 가능
            # context.abort(grpc.StatusCode.INTERNAL, f"MockLLM internal error: {e}")

        with self._lock:
            print(f"MockLLM: [{current_session}] Stream processing finished. Received {len(self.received_chunks)} chunks.")
            self.stream_finished = True
            # 테스트 검증을 위해 마지막 청크 정보나 요약 정보 출력 가능
            if self.received_chunks:
                last_chunk_info = self.received_chunks[-1]
                print(f"MockLLM: [{current_session}] Last chunk info: req_num={last_chunk_info['req_num']}, final={last_chunk_info['is_final']}, len={last_chunk_info['text_len']}")

        # 성공 응답 반환
        summary = llm_engine_pb2.LLMStreamSummary(success=True, message="Mock processing complete")
        return summary

    # 참고: 외부 테스트 스크립트에서 Mock 상태를 확인/제어하려면 별도의
    #      gRPC 서비스나 다른 통신 방식(HTTP 등)을 추가해야 합니다.
    #      예: def GetState(self, request, context): ...
    #      예: def ResetState(self, request, context): ...

def serve():
    """gRPC 서버를 생성하고 실행합니다."""
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    mock_service = MockLLMEngineService()
    llm_engine_pb2_grpc.add_LLMServiceServicer_to_server(mock_service, server)

    server.add_insecure_port(MOCK_SERVER_ADDRESS)
    print(f"Mock LLM Server listening on {MOCK_SERVER_ADDRESS}")
    server.start()

    try:
        # 서버가 종료될 때까지 대기 (Ctrl+C 등으로 종료)
        server.wait_for_termination()
    except KeyboardInterrupt:
        print("Stopping Mock LLM server...")
        server.stop(grace=1) # 1초간 graceful shutdown 대기
        print("Mock LLM server stopped.")

if __name__ == '__main__':
    serve()