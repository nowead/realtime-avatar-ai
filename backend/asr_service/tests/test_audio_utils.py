import os
import sys

print("=== sys.path ===")
for p in sys.path:
    print(p)
print("================")

from app.audio_utils import convert_to_wav


def test_convert_to_wav():
    input_file = "samples/test.wav"
    output_file = "tmp/test_output.wav"

    convert_to_wav(input_file, output_file)

    assert os.path.exists(output_file)
    assert os.path.getsize(output_file) > 0

    os.remove(output_file)
