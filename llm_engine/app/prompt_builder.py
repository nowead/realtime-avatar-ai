from typing import List, Dict


def build_prompt(
    session_history: List[Dict[str, str]], new_input: str
) -> List[Dict[str, str]]:
    """
    OpenAI ChatCompletion용 메시지 리스트 구성
    """
    prompt = [
        {"role": "system", "content": "You are a helpful and friendly AI assistant."}
    ]
    prompt += session_history  # 이전 대화 내용
    prompt.append({"role": "user", "content": new_input})
    return prompt
