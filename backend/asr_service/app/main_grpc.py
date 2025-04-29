import grpc
from concurrent import futures
import os
import uuid

import whisper_runner, audio_utils
import protos.asr_pb2 as asr_pb2
import protos.asr_pb2_grpc as asr_pb2_grpc


class ASRService(asr_pb2_grpc.ASRServiceServicer):
    def Transcribe(self, request, context):
        raw_ext = request.format
        raw_path = f"/tmp/{uuid.uuid4().hex}.{raw_ext}"
        wav_path = raw_path.replace(f".{raw_ext}", ".wav")

        with open(raw_path, "wb") as f:
            f.write(request.audio_data)

        try:
            audio_utils.convert_to_wav(raw_path, wav_path)
            result = whisper_runner.run_whisper(wav_path)
            return asr_pb2.TranscriptionResponse(text=result)
        except Exception as e:
            context.set_details(str(e))
            context.set_code(grpc.StatusCode.INTERNAL)
            return asr_pb2.TranscriptionResponse()
        finally:
            os.remove(raw_path)
            if os.path.exists(wav_path):
                os.remove(wav_path)


def serve():
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=4))
    asr_pb2_grpc.add_ASRServiceServicer_to_server(ASRService(), server)
    server.add_insecure_port("[::]:50051")
    server.start()
    print("gRPC server started on port 50051")
    server.wait_for_termination()


if __name__ == "__main__":
    serve()
