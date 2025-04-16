import os
from app.whisper_runner import run_whisper


def test_run_whisper():
    # whisper.cpp는 test.wav에 대해 transcribe한 결과 텍스트를 생성
    sample_wav = "samples/test.wav"
    text = run_whisper(sample_wav)

    assert isinstance(text, str)
    assert len(text.strip()) > 0
