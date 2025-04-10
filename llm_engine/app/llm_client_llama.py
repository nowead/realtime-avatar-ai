import os
from llama_cpp import Llama
from prompt_builder import build_prompt
from session_manager import get_session_history, append_session_history

# 로컬 모델 초기화
MODEL_PATH = os.getenv("LLAMA_MODEL_PATH", "models/llama-2-7b-chat.Q4_K_M.gguf")

llm = Llama(
    model_path=MODEL_PATH,
    n_ctx=2048,
    n_threads=8,  # 코어 수에 따라 조절
    use_mlock=True,  # 메모리 고정 (성능 ↑)
    verbose=False,
)


async def get_llm_response(user_input: str, session_id: str) -> str:
    history = get_session_history(session_id)
    prompt_list = build_prompt(history, user_input)

    # llama.cpp는 일반적인 prompt string 기반
    prompt_text = ""
    for msg in prompt_list:
        if msg["role"] == "system":
            prompt_text += f"<<SYS>>\n{msg['content']}\n<</SYS>>\n"
        elif msg["role"] == "user":
            prompt_text += f"[INST] {msg['content']} [/INST]\n"
        elif msg["role"] == "assistant":
            prompt_text += f"{msg['content']}\n"

    output = llm(prompt_text, stop=["</s>"])
    reply = output["choices"][0]["text"].strip()

    append_session_history(session_id, "user", user_input)
    append_session_history(session_id, "assistant", reply)

    return reply
