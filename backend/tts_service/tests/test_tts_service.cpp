#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

#include <grpcpp/grpcpp.h>
#include "tts.grpc.pb.h" // 생성된 gRPC 헤더

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReaderWriter;
using grpc::Status;

class TTSStreamClient {
public:
    TTSStreamClient(std::shared_ptr<Channel> channel)
      : stub_(tts::TTSService::NewStub(channel)) {}

    bool SynthesizeStreamToFile(
        const std::vector<std::string>& text_chunks,
        const std::string& voice,
        const std::string& out_path
    ) {
        tts::TTSStreamRequest request;
        tts::TTSStreamResponse response;
        ClientContext context;

        std::shared_ptr<ClientReaderWriter<tts::TTSStreamRequest, tts::TTSStreamResponse>> stream(
            stub_->SynthesizeStream(&context));

        // 1. 설정 정보 전송
        std::cout << "📤 Sending config: Voice=" << voice << std::endl;
        request.mutable_config()->set_voice(voice);
        if (!stream->Write(request)) {
             std::cerr << "❌ Failed to write config to stream." << std::endl;
             // 초기 설정 전송 실패 시 스트림 종료 시도 (선택적)
             stream->Finish(); // 상태 확인 전에 스트림 리소스 정리 시도
             return false;
        }

        // 2. 텍스트 청크 스트리밍 전송
         std::cout << "📤 Sending text chunks..." << std::endl;
        for (const auto& chunk : text_chunks) {
            // 각 요청마다 request 객체를 새로 만들거나 clear 후 재사용
            request.Clear(); // 이전 상태 클리어
            request.set_text_chunk(chunk);
            if (!stream->Write(request)) {
                std::cerr << "❌ Failed to write text chunk to stream." << std::endl;
                stream->WritesDone();
                stream->Finish(); // 상태 확인 전에 스트림 리소스 정리 시도
                return false;
            }
             // std::cout << "   Sent chunk: " << chunk.substr(0, 20) << "..." << std::endl;
             std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // 3. 텍스트 전송 완료 알림
        std::cout << "📤 Finished sending text (WritesDone)." << std::endl;
        if (!stream->WritesDone()) {
             std::cerr << "❌ Failed to call WritesDone on stream." << std::endl;
             // WritesDone 실패 시에도 Finish 시도
             stream->Finish();
             return false;
        }


        // 4. 서버로부터 응답 스트림 수신
        std::cout << "📥 Receiving response stream..." << std::endl;
        std::ofstream out_file(out_path, std::ios::binary);
        if (!out_file.is_open()) {
             std::cerr << "❌ Failed to open output file: " << out_path << std::endl;
             // 파일 열기 실패 시에도 스트림은 계속 읽어야 함
        }

        int audio_chunk_count = 0;
        size_t total_audio_bytes = 0;
        int viseme_count = 0;

        while (stream->Read(&response)) {
            // --- oneof 필드 확인 방식 변경 ---
            switch (response.response_data_case()) {
                case tts::TTSStreamResponse::kAudioChunk:
                    audio_chunk_count++;
                    total_audio_bytes += response.audio_chunk().size();
                    if (out_file.is_open()) {
                        out_file.write(response.audio_chunk().data(), response.audio_chunk().size());
                    }
                    break;
                case tts::TTSStreamResponse::kViseme:
                    viseme_count++;
                    std::cout << "   🗣️ Received Viseme " << viseme_count << ": ID=" << response.viseme().id()
                              << ", Time=" << response.viseme().time_ms() << "ms" << std::endl;
                    break;
                case tts::TTSStreamResponse::RESPONSE_DATA_NOT_SET:
                default:
                    // 데이터 없는 응답 무시
                    break;
            }
             // --- oneof 필드 확인 방식 변경 끝 ---
        }
        if (out_file.is_open()) {
            out_file.close();
            std::cout << "   File closed: " << out_path << std::endl;
        } else {
             std::cerr << "   Warning: Output file was not opened, audio data discarded." << std::endl;
        }


        // 5. 최종 상태 확인
        Status status = stream->Finish();
        if (status.ok()) {
            std::cout << "✅ Stream finished successfully." << std::endl;
            std::cout << "   Total audio chunks received: " << audio_chunk_count << std::endl;
            std::cout << "   Total audio bytes received: " << total_audio_bytes << std::endl;
            std::cout << "   Total visemes received: " << viseme_count << std::endl;
            if (total_audio_bytes > 0 && out_file.is_open()) { // 파일이 정상적으로 열렸었는지 추가 확인
                 std::cout << "   Audio saved to: " << out_path << std::endl;
            }
            return true;
        } else {
            std::cerr << "❌ Stream finished with error (" << status.error_code()
                      << "): " << status.error_message() << std::endl;
            return false;
        }
    }

private:
    std::unique_ptr<tts::TTSService::Stub> stub_;
};

int main(int argc, char** argv) {
    std::string server_address = "tts-service:50055";
    if (argc > 1) {
        server_address = argv[1];
    }
     std::cout << "Connecting to server at " << server_address << std::endl;

    TTSStreamClient client(
      grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials())
    );

    std::vector<std::string> text_chunks = {
        "안녕하세요, ",
        "gRPC 스트리밍 테스트입니다. ",
        "실시간으로 음성이 합성되어 ",
        "전달되는지 확인해 보겠습니다"
    };
    std::string voice = "ko-KR-SoonBokNeural";
    std::string output_filename = "/app/tests/output_stream.pcm";

    bool success = client.SynthesizeStreamToFile(text_chunks, voice, output_filename);

    return success ? 0 : 1;
}