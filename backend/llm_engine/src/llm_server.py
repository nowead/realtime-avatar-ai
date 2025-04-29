import asyncio
import grpc
import openai # 최신 버전(v1.0 이상)이 설치되어 있다고 가정
import os
import time
from concurrent import futures
from dotenv import load_dotenv

# --- Health Checking 관련 Import ---
from grpc_health.v1 import health_pb2, health_pb2_grpc
from grpc_health.v1.health import HealthServicer
# --- Health Checking 관련 Import 끝 ---

# 생성된 pb2 및 pb2_grpc 파일 임포트
from generated.llm_pb2 import ChatRequest, ChatResponse
from generated.llm_pb2_grpc import LLMServiceServicer, add_LLMServiceServicer_to_server

# --- OpenAI V1.x 클라이언트 및 httpx Import ---
from openai import AsyncOpenAI
import httpx # httpx 임포트 추가
# --- ---

# OpenAI 설정
load_dotenv()

# ✅ 세션별 대화 히스토리 및 마지막 사용 시간 관리
session_histories = {}
session_last_used = {}

# 세션 유지 시간 (초) - 10분 (600초)
SESSION_TIMEOUT = 600

# 세션 수 제한 - 1000개
MAX_SESSIONS = 1000

# 세션 저장 경로
SAVE_DIR = "saved_sessions"
os.makedirs(SAVE_DIR, exist_ok=True)

# --- OpenAI 클라이언트 생성 (summarize_and_save 용) ---
# 이 함수에서도 프록시 비활성화된 클라이언트 사용하도록 수정
def get_openai_client():
    transport = httpx.AsyncHTTPTransport(retries=2)
    proxies_disabled_http_client = httpx.AsyncClient(
        proxies=None, 
        transport=transport,
        timeout=60.0 # 요약은 더 길게 설정 가능
    )
    return AsyncOpenAI(
        api_key=os.getenv("OPENAI_API_KEY"),
        http_client=proxies_disabled_http_client
    )
# --- ---

def build_prompt(session_id, user_input, max_turns=3):
    history = session_histories.get(session_id, [])
    recent_history = history[-max_turns * 2:]

    prompt = []
    for role, content in recent_history:
        prompt.append({"role": role, "content": content})

    prompt.append({"role": "user", "content": user_input})

    return prompt

def update_session_history(session_id, role, content):
    if session_id not in session_histories:
        session_histories[session_id] = []
    session_histories[session_id].append((role, content))
    session_last_used[session_id] = time.time()

def save_session_to_file(session_id):
    history = session_histories.get(session_id, [])
    if not history:
        return

    save_path = os.path.join(SAVE_DIR, f"{session_id}.txt")
    with open(save_path, "w", encoding="utf-8") as f:
        for role, content in history:
            f.write(f"{role.upper()}: {content}\n\n")

    print(f"📦 [Session {session_id}] 원본 저장 완료: {save_path}")

async def summarize_and_save(session_id):
    history = session_histories.get(session_id, [])
    if not history:
        return

    full_conversation = ""
    for role, content in history:
        full_conversation += f"{role}: {content}\n"

    try:
        # --- 수정된 클라이언트 생성 방식 사용 ---
        client = get_openai_client() 
        # --- ---
        response = await client.chat.completions.create( 
            model="gpt-3.5-turbo",
            messages=[
                {"role": "system", "content": "다음 대화 내용을 간결하게 요약해줘."},
                {"role": "user", "content": full_conversation}
            ]
        )
        summary = response.choices[0].message.content.strip() if response.choices else "요약 실패"

        save_path = os.path.join(SAVE_DIR, f"{session_id}_summary.txt")
        with open(save_path, "w", encoding="utf-8") as f:
            f.write(summary)

        print(f"📝 [Session {session_id}] 요약 저장 완료: {save_path}")

    except openai.APIError as e:
         print(f"⚠️  [Session {session_id}] 요약 실패 (OpenAI API Error): {e}")
    except openai.APIConnectionError as e:
         print(f"⚠️  [Session {session_id}] 요약 실패 (OpenAI Connection Error): {e}")
    except Exception as e:
        print(f"⚠️  [Session {session_id}] 요약 실패 (Unexpected Error): {e}")

async def clean_old_sessions():
    while True:
        now = time.time()
        expired_sessions = [
            session_id for session_id, last_used in list(session_last_used.items()) 
            if now - last_used > SESSION_TIMEOUT
        ]
        for session_id in expired_sessions:
            if session_id in session_histories:
                save_session_to_file(session_id)
                await summarize_and_save(session_id)
                session_histories.pop(session_id, None)
                session_last_used.pop(session_id, None)
                print(f"🗑️  [Session {session_id}] 오래되어 삭제됨")

        if len(session_histories) > MAX_SESSIONS:
            sorted_sessions = sorted(list(session_last_used.items()), key=lambda x: x[1])
            overflow = len(session_histories) - MAX_SESSIONS
            sessions_to_remove = [sid for sid, _ in sorted_sessions[:overflow]]

            for session_id in sessions_to_remove:
                 if session_id in session_histories:
                    save_session_to_file(session_id)
                    await summarize_and_save(session_id)
                    session_histories.pop(session_id, None)
                    session_last_used.pop(session_id, None)
                    print(f"🧹 [Session {session_id}] 세션 수 초과로 삭제됨")

        await asyncio.sleep(60)

class LLMService(LLMServiceServicer):
    # --- OpenAI V1.x: 클래스 레벨에서 httpx 클라이언트를 설정하여 전달 (수정됨) ---
    def __init__(self):
       # 1. 프록시가 비활성화된 httpx 클라이언트 생성
       transport = httpx.AsyncHTTPTransport(retries=2)
       proxies_disabled_http_client = httpx.AsyncClient(
           proxies=None,  # 프록시 비활성화
           transport=transport,
           timeout=30.0  # 예시 타임아웃
       )
       # 2. 생성된 httpx 클라이언트를 AsyncOpenAI의 http_client 인자로 전달
       self.client = AsyncOpenAI(
           api_key=os.getenv("OPENAI_API_KEY"),
           http_client=proxies_disabled_http_client
       )
       print("🤖 OpenAI client initialized (proxies explicitly disabled via http_client).")
    # --- ---

    async def ChatStream(self, request_iterator, context):
        # __init__에서 생성한 클라이언트 사용
        client = self.client 

        async for request in request_iterator:
            session_id = request.session_id
            user_text = request.user_text

            print(f"📝 [Session {session_id}] User: {user_text}")

            prompt = build_prompt(session_id, user_text)

            try:
                response_stream = await client.chat.completions.create(
                    model="gpt-3.5-turbo",
                    messages=prompt,
                    stream=True,
                )
                
                assistant_reply = ""
                chunk_count = 0
                async for chunk in response_stream:
                    chunk_count += 1
                    content = chunk.choices[0].delta.content
                    if content:
                        assistant_reply += content
                        try:
                            await context.write(
                                ChatResponse(
                                    assistant_text=content,
                                    is_final=False,
                                )
                            )
                        except grpc.aio.AioRpcError as e:
                            print(f"⚠️ [Session {session_id}] Failed to write to stream: {e}")
                            return

                print(f"✅ [Session {session_id}] Assistant: (received {chunk_count} chunks) {assistant_reply[:50]}...")

                update_session_history(session_id, "user", user_text)
                update_session_history(session_id, "assistant", assistant_reply)

                # if not context.is_active():
                #      print(f"⚠️ [Session {session_id}] Client disconnected before sending final response.")
                #      return

                await context.write(
                    ChatResponse(
                        assistant_text="",
                        is_final=True,
                    )
                )
            # --- OpenAI V1.x 오류 처리 방식 ---
            except openai.APIError as e: 
                 print(f"❌ [Session {session_id}] OpenAI API returned an API Error: {e}")
                 await context.abort(grpc.StatusCode.INTERNAL, f"OpenAI API Error: {e}")
            except openai.APIConnectionError as e: 
                 print(f"❌ [Session {session_id}] Failed to connect to OpenAI API: {e}")
                 await context.abort(grpc.StatusCode.UNAVAILABLE, f"OpenAI Connection Error: {e}")
            except Exception as e: 
                 print(f"❌ [Session {session_id}] Unexpected Error in ChatStream: {e}")
                 await context.abort(grpc.StatusCode.INTERNAL, f"Unexpected server error.")
            # --- ---

async def serve():
    health_servicer = HealthServicer(experimental_non_blocking=True)
    server = grpc.aio.server()
    # LLMService 인스턴스 생성 시 __init__ 호출됨
    add_LLMServiceServicer_to_server(LLMService(), server) 
    health_pb2_grpc.add_HealthServicer_to_server(health_servicer, server)
    listen_addr = '[::]:50051'
    server.add_insecure_port(listen_addr)

    print("Starting server...")
    await server.start()
    print(f"✅ gRPC server started at {listen_addr}")

    health_servicer.set("", health_pb2.HealthCheckResponse.ServingStatus.SERVING)
    health_servicer.set("llm.LLMService", health_pb2.HealthCheckResponse.ServingStatus.SERVING)
    print("✅ Health status set to SERVING")

    cleaner_task = asyncio.create_task(clean_old_sessions())

    try:
        await server.wait_for_termination()
    except asyncio.CancelledError:
         print("Server task cancelled.")
    finally:
        print("Stopping server...")
        health_servicer.set_overall_serving_status(health_pb2.HealthCheckResponse.ServingStatus.NOT_SERVING)
        await server.stop(grace=5)
        cleaner_task.cancel()
        try:
            await cleaner_task
        except asyncio.CancelledError:
            pass
        print("Server stopped gracefully.")

if __name__ == "__main__":
    try:
        asyncio.run(serve())
    except KeyboardInterrupt:
        print("KeyboardInterrupt received, stopping.")
    except Exception as e:
        print(f"Unhandled exception: {e}")