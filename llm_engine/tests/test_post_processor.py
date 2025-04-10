from app.post_processor import process_response


def test_post_processor_strip():
    raw = "  안녕하세요! \n"
    cleaned = process_response(raw)
    assert cleaned == "안녕하세요!"
