from app.session_manager import (
    get_session_history,
    append_session_history,
    clear_session,
)


def test_session_add_and_get():
    session_id = "test-session"
    clear_session(session_id)

    append_session_history(session_id, "user", "hi")
    append_session_history(session_id, "assistant", "hello")

    history = get_session_history(session_id)
    assert len(history) == 2
    assert history[0]["role"] == "user"
    assert history[1]["content"] == "hello"
