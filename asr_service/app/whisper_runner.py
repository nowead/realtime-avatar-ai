import subprocess
import os


def run_whisper(audio_path: str, model_path: str = "whisper.cpp/models/ggml-tiny.bin") -> str:
    whisper_bin = os.path.abspath("whisper.cpp/build/bin/whisper-cli")
    model_path = os.path.abspath(model_path)
    audio_path = os.path.abspath(audio_path)

    result = subprocess.run(
        [
            whisper_bin,
            "-m", model_path,
            "-f", audio_path,
            "-otxt",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    print("=== whisper stdout ===")
    print(result.stdout)
    print("=== whisper stderr ===")
    print(result.stderr)

    txt_path = audio_path + ".txt"
    if not os.path.exists(txt_path):
        raise RuntimeError("Whisper transcription failed.")

    with open(txt_path, "r") as f:
        content = f.read()
    os.remove(txt_path)
    return content.strip()
