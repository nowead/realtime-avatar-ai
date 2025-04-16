from TTS.api import TTS
from io import BytesIO
import soundfile as sf

# 미리 모델 로딩
tts = TTS(model_name="tts_models/ko/kss/tacotron2-DDC")

def synthesize(text, output_path):
    """텍스트를 음성으로 변환하여 파일로 저장"""
    tts.tts_to_file(text=text, file_path=output_path)
    return output_path

def synthesize_to_memory(text, speaker="default"):
    """텍스트를 음성으로 변환하여 bytes로 반환"""
    wav, sample_rate = tts.tts(text)

    # 메모리 버퍼에 WAV 저장
    buffer = BytesIO()
    sf.write(buffer, wav, sample_rate, format='WAV')
    return buffer.getvalue()
