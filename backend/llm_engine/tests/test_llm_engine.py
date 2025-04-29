import asyncio
import grpc
import pytest

from generated import llm_pb2, llm_pb2_grpc

# ✅ pytest-asyncio 사용
pytestmark = pytest.mark.asyncio

@pytest.mark.asyncio
async def test_llm_engine_chatstream():
    async with grpc.aio.insecure_channel('llm-engine:50051') as channel:
        stub = llm_pb2_grpc.LLMServiceStub(channel)

        async def request_gen():
            yield llm_pb2.ChatRequest(
                session_id="test_session_id",
                user_text="Hello, who are you?"
            )

        response_stream = stub.ChatStream(request_gen())

        full_response = ""
        async for response in response_stream:
            if response.is_final:
                break
            full_response += response.assistant_text

        # ✅ 결과 확인
        print(f"✅ Assistant replied: {full_response}")
