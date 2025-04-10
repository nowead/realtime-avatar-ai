import pytest
from unittest.mock import patch
from app.llm_client import get_llm_response


@pytest.mark.asyncio
@patch("app.llm_client.openai.ChatCompletion.create")
async def test_llm_response(mock_create):
    mock_create.return_value = {
        "choices": [{"message": {"content": "테스트 응답입니다."}}]
    }

    result = await get_llm_response("안녕", "mock-session")
    assert "테스트" in result
