from collections import defaultdict
from typing import List, Dict

# session_id → history (list of message dicts)
_session_store = defaultdict(list)


def get_session_history(session_id: str) -> List[Dict[str, str]]:
    return _session_store[session_id]


def append_session_history(session_id: str, role: str, content: str):
    _session_store[session_id].append({"role": role, "content": content})


def clear_session(session_id: str):
    _session_store[session_id].clear()
