#include <gtest/gtest.h>
#include <grpcpp/grpcpp.h>
#include "../src/tts_service.h"  // TTSServiceImpl
#include "../src/tts_engine.h"
#include "../generated/tts.grpc.pb.h"

#include <thread>
#include <chrono>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using namespace tts;

class TTSClient {
public:
    TTSClient(std::shared_ptr<Channel> channel)
        : stub(TTSService::NewStub(channel)) {}

    TTSResponse SynthesizeText(const std::string& text, const std::string& voice) {
        TTSRequest request;
        request.set_text(text);
        request.set_voice(voice);

        TTSResponse response;
        ClientContext context;

        Status status = stub->Synthesize(&context, request, &response);
        if (!status.ok()) {
            throw std::runtime_error("RPC failed: " + status.error_message());
        }
        return response;
    }

private:
    std::unique_ptr<TTSService::Stub> stub;
};

void RunTestServer(const std::string& server_address, std::unique_ptr<Server>& server) {
    TTSServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    server = builder.BuildAndStart();
}

// 🧪 gRPC 통합 테스트
TEST(GRPCTTSTest, ShouldRespondWithWavData) {
    const std::string server_address("localhost:50055");

    // 서버 실행
    std::unique_ptr<Server> server;
    std::thread server_thread([&]() {
        RunTestServer(server_address, server);
    });

    // 서버 준비 시간 확보
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 클라이언트 테스트
    TTSClient client(grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials()));

    const std::string text = "이것은 gRPC 통합 테스트 문장입니다.";
    const std::string voice = "default";

    TTSResponse response = client.SynthesizeText(text, voice);

    ASSERT_FALSE(response.audio_data().empty()) << "오디오 응답이 비어 있음";
    ASSERT_EQ(response.format(), "wav") << "응답 포맷이 wav가 아님";

    const std::string& audio = response.audio_data();
    ASSERT_GE(audio.size(), 1024) << "오디오 응답 크기가 너무 작음";

    // WAV 헤더 확인
    ASSERT_EQ(audio[0], 'R');
    ASSERT_EQ(audio[1], 'I');
    ASSERT_EQ(audio[2], 'F');
    ASSERT_EQ(audio[3], 'F');

    // 서버 종료
    server->Shutdown();
    server_thread.join();
}
