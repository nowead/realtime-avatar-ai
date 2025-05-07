# tests/mock_avatar_sync_server.py
import grpc
from concurrent import futures
import time
import os
import sys
from threading import Lock

# 생성된 Python gRPC 코드 import
try:
    # 현재 mock_avatar_sync_server.py는 tests/ 안에 있으므로,
    # Dockerfile.external_test에서 생성된 코드를 참조하려면
    # PYTHONPATH에 /app 이 포함되어 있어야 함 (기본적으로 포함될 수 있음)
    # 또는 상대 경로 import 사용 (비추천)
    import avatar_sync_pb2
    import avatar_sync_pb2_grpc
    from google.protobuf import empty_pb2
    from google.protobuf import timestamp_pb2
except ImportError as e:
    print(f"Error: Failed to import generated protobuf code for AvatarSync: {e}", file=sys.stderr)
    print("Python Path:", sys.path, file=sys.stderr)
    # /app 디렉토리 내용 확인 (Dockerfile.external_test 에서 코드 생성 위치)
    try:
        print("/app directory contents:", os.listdir('/app'), file=sys.stderr)
    except FileNotFoundError:
        print("/app directory not found.", file=sys.stderr)
    sys.exit(1)

MOCK_AVATAR_SYNC_PORT = os.getenv("MOCK_AVATAR_SYNC_PORT", "50053") # TTS가 연결할 포트
MOCK_AVATAR_SYNC_ADDRESS = f'[::]:{MOCK_AVATAR_SYNC_PORT}'

class MockAvatarSyncService(avatar_sync_pb2_grpc.AvatarSyncServiceServicer):
    def __init__(self):
        self._lock = Lock()
        self.received_audio_chunks = 0
        self.received_visemes = 0
        self.current_session_id = None
        self.stream_finished = False
        self.request_count = 0
        self.config_received = False
        print("MockAvatarSyncService initialized.")

    def SyncAvatarStream(self, request_iterator, context):
        peer = context.peer()
        print(f"MockAvatarSync: Connection received from {peer} for SyncAvatarStream.")
        self.config_received = False
        self.received_audio_chunks = 0
        self.received_visemes = 0
        self.current_session_id = "unknown_session"
        self.request_count = 0

        try:
            for request in request_iterator:
                with self._lock:
                    self.request_count += 1
                    request_type = request.WhichOneof('request_data')

                    if not self.config_received:
                        if request_type == 'config':
                            self.current_session_id = request.config.session_id
                            self.config_received = True
                            print(f"MockAvatarSync: [{self.current_session_id}] Received SyncConfig.")
                        else:
                            print(f"MockAvatarSync: Error! Expected SyncConfig as first message, got {request_type}.", file=sys.stderr)
                            context.abort(grpc.StatusCode.INVALID_ARGUMENT, "Expected SyncConfig first.")
                            return empty_pb2.Empty()
                    else: # Config already received
                        if request_type == 'audio_chunk':
                            self.received_audio_chunks += 1
                            # print(f"MockAvatarSync: [{self.current_session_id}] Received audio chunk, size: {len(request.audio_chunk)}")
                        elif request_type == 'viseme_data':
                            self.received_visemes += 1
                            viseme = request.viseme_data
                            ts_str = "N/A"
                            if viseme.HasField("start_time"):
                                ts = viseme.start_time
                                ts_str = f"{ts.seconds}.{ts.nanos // 1000000:03d}s" # 밀리초까지 표현
                            # print(f"MockAvatarSync: [{self.current_session_id}] Received VisemeData: ID={viseme.viseme_id}, Start={ts_str}, Duration={viseme.duration_sec:.3f}s")
                        elif request_type == 'config':
                            print(f"MockAvatarSync: [{self.current_session_id}] Warning: Received SyncConfig again after stream started. Ignoring.", file=sys.stderr)
                        else:
                            print(f"MockAvatarSync: [{self.current_session_id}] Warning: Received unknown message type '{request_type}'.", file=sys.stderr)

        except grpc.RpcError as e:
            print(f"MockAvatarSync: [{self.current_session_id}] RpcError during stream: {e.code()} - {e.details()}", file=sys.stderr)
        except Exception as e:
            print(f"MockAvatarSync: [{self.current_session_id}] Unexpected error: {e}", file=sys.stderr)
            context.abort(grpc.StatusCode.INTERNAL, f"MockAvatarSync internal error: {e}")
        finally:
            with self._lock:
                self.stream_finished = True
                print(f"MockAvatarSync: [{self.current_session_id}] Stream processing finished. "
                      f"Audio chunks: {self.received_audio_chunks}, Visemes: {self.received_visemes}")
        return empty_pb2.Empty()

    def get_stats(self):
        with self._lock:
            return {
                "session_id": self.current_session_id,
                "audio_chunks": self.received_audio_chunks,
                "visemes": self.received_visemes,
                "config_received": self.config_received,
                "stream_finished": self.stream_finished,
                "total_requests": self.request_count
            }

    def reset_stats(self):
        with self._lock:
            self.received_audio_chunks = 0
            self.received_visemes = 0
            self.current_session_id = None
            self.stream_finished = False
            self.request_count = 0
            self.config_received = False
            print("MockAvatarSync: Stats reset.")


# Flask 앱 추가 (상태 조회 및 리셋 용도 - 선택 사항)
from flask import Flask, jsonify
app = Flask(__name__)
mock_service_instance = MockAvatarSyncService() # 서비스 인스턴스 공유

@app.route('/stats', methods=['GET'])
def get_server_stats():
    return jsonify(mock_service_instance.get_stats())

@app.route('/reset', methods=['POST'])
def reset_server_stats():
    mock_service_instance.reset_stats()
    return jsonify({"status": "reset successful"})


def serve_grpc():
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    avatar_sync_pb2_grpc.add_AvatarSyncServiceServicer_to_server(mock_service_instance, server)
    server.add_insecure_port(MOCK_AVATAR_SYNC_ADDRESS)
    print(f"Mock AvatarSync gRPC Server listening on {MOCK_AVATAR_SYNC_ADDRESS}")
    server.start()
    # server.wait_for_termination() # Flask와 함께 실행 시 블로킹 문제 발생 가능
    return server

def serve_flask():
    # Flask는 개발 서버이므로 프로덕션에는 Gunicorn 등 사용
    flask_port = int(os.getenv("MOCK_AVATAR_SYNC_HTTP_PORT", "50083"))
    print(f"Mock AvatarSync Flask Server for stats listening on http://localhost:{flask_port}")
    app.run(host='0.0.0.0', port=flask_port, debug=False)


if __name__ == '__main__':
    print("--- MockAvatarSync Debug Info ---")
    print("Current Working Directory:", os.getcwd())
    print("Python Path:", sys.path)
    print("Checking for generated files in /app (expected by Dockerfile.external_test):")
    try:
        app_files = os.listdir('/app') # Dockerfile.external_test 에서 /app 으로 복사
        if 'avatar_sync_pb2.py' not in app_files or 'avatar_sync_pb2_grpc.py' not in app_files:
             print("WARNING: avatar_sync_pb2.py or _grpc.py not found in /app", file=sys.stderr)
        else:
            print("Found avatar_sync_pb2.py and _grpc.py in /app")
    except FileNotFoundError:
         print("ERROR: /app directory not found (running outside Docker?).", file=sys.stderr)
    except Exception as e:
         print(f"ERROR checking /app contents: {e}", file=sys.stderr)
    print("--- Starting Servers ---")

    grpc_server = serve_grpc()

    # Flask 서버를 별도 스레드에서 실행 (gRPC 서버와 동시 실행)
    from threading import Thread
    flask_thread = Thread(target=serve_flask, daemon=True)
    flask_thread.start()

    try:
        grpc_server.wait_for_termination()
    except KeyboardInterrupt:
        print("Stopping Mock AvatarSync gRPC server...")
        grpc_server.stop(grace=1)
        print("Mock AvatarSync gRPC server stopped.")
    # Flask 스레드는 데몬이므로 주 스레드 종료 시 자동 종료