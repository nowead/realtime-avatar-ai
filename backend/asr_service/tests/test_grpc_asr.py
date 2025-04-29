import grpc
from protos import asr_pb2
from protos import asr_pb2_grpc


def test_grpc_transcribe():
    with open("samples/test.wav", "rb") as f:
        audio_bytes = f.read()

    channel = grpc.insecure_channel("localhost:50051")
    stub = asr_pb2_grpc.ASRServiceStub(channel)

    request = asr_pb2.AudioRequest(audio_data=audio_bytes, format="wav")
    response = stub.Transcribe(request)

    assert isinstance(response.text, str)
    assert len(response.text.strip()) > 0
