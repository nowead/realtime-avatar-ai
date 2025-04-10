from app.prompt_builder import build_prompt


def test_prompt_building():
    history = [
        {"role": "user", "content": "안녕"},
        {"role": "assistant", "content": "안녕하세요!"},
    ]
    new_input = "오늘 날씨 어때?"

    result = build_prompt(history, new_input)

    assert result[0]["role"] == "system"
    assert result[-1] == {"role": "user", "content": "오늘 날씨 어때?"}
    assert len(result) == 4
