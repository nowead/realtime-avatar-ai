import os
import openai
from dotenv import load_dotenv

from app.session_manager import get_session_history, append_session_history
from app.prompt_builder import build_prompt

load_dotenv()
openai.api_key = os.getenv("OPENAI_API_KEY")


async def get_llm_response(user_input: str, session_id: str) -> str:
    history = get_session_history(session_id)
    prompt = build_prompt(history, user_input)

    response = openai.ChatCompletion.create(model="gpt-3.5-turbo", messages=prompt)

    reply = response["choices"][0]["message"]["content"]

    # 세션 업데이트
    append_session_history(session_id, "user", user_input)
    append_session_history(session_id, "assistant", reply)

    return reply
