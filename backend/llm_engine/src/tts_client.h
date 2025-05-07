// llm_engine/src/tts_client.h

#ifndef TTS_CLIENT_H
#define TTS_CLIENT_H

#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <grpcpp/grpcpp.h>
#include <google/protobuf/empty.pb.h>

#include "tts.grpc.pb.h"

namespace llm_engine {

using grpc::Channel;
using grpc::ClientContext;
// === using 선언 수정 ===
using grpc::ClientWriterInterface; // ClientWriter 대신 ClientWriterInterface 사용
using grpc::Status;
// === tts 네임스페이스 ===
using tts::TTSService;
using tts::TTSStreamRequest;
using tts::SynthesisConfig;

class TTSClient {
public:
    explicit TTSClient(std::shared_ptr<Channel> channel);
    explicit TTSClient(std::shared_ptr<TTSService::StubInterface> stub);

    bool StartStream(const std::string& session_id, const std::string& language_code, const std::string& voice_name);
    bool SendTextChunk(const std::string& text);
    Status FinishStream();
    bool IsStreamActive() const;

    // SynthesizeSpeech 함수 제거됨

private:
    std::shared_ptr<TTSService::StubInterface> stub_;
    std::string server_address_;
    std::string session_id_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<grpc::ClientContext> context_;
    // === stream_ 멤버 변수 타입 확인 (ClientWriterInterface 사용) ===
    std::unique_ptr<ClientWriterInterface<TTSStreamRequest>> stream_; // 이제 using 선언과 일치
    google::protobuf::Empty server_response_;
    mutable std::mutex stream_mutex_;
    std::atomic<bool> stream_active_{false};
};

} // namespace llm_engine

#endif // TTS_CLIENT_H