#include <iostream>
#include <fstream>
#include <memory>
#include <grpcpp/grpcpp.h>
#include "tts.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

class TTSClient {
public:
    TTSClient(std::shared_ptr<Channel> channel)
      : stub(tts::TTSService::NewStub(channel)) {}

    bool SynthesizeToFile(
        const std::string& text,
        const std::string& voice,
        const std::string& format,
        const std::string& out_path
    ) {
        tts::TTSRequest req;
        req.set_text(text);
        req.set_voice(voice);
        req.set_format(format);

        tts::TTSResponse resp;
        ClientContext ctx;
        // 타임아웃 15초로 연장
        ctx.set_deadline(
          std::chrono::system_clock::now() +
          std::chrono::seconds(15)
        );

        Status status = stub->Synthesize(&ctx, req, &resp);
        if (!status.ok()) {
            std::cerr << "❌ gRPC error (" << status.error_code()
                      << "): " << status.error_message() << std::endl;
            return false;
        }

        std::ofstream out(out_path, std::ios::binary);
        out.write(resp.audio_data().data(),
                  resp.audio_data().size());
        out.close();
        std::cout << "✅ Saved audio to " << out_path << "\n";

        if (resp.visemes_size()) {
            std::cout << "Visemes:\n";
            for (auto& e : resp.visemes()) {
                std::cout << "  ID=" << e.id()
                          << ", time_ms=" << e.time_ms() << "\n";
            }
        }
        return true;
    }

private:
    std::unique_ptr<tts::TTSService::Stub> stub;
};

int main() {
    TTSClient client(
      grpc::CreateChannel("tts-service:50055",
                          grpc::InsecureChannelCredentials())
    );
    return client.SynthesizeToFile(
      "딱 걸렸어. 널 보는 내 눈빛이 무심한척 잘 숨겨왔었는데",
      "ko-KR-SoonBokNeural",
      "wav",
      "output.wav"
    ) ? 0 : 1;
}