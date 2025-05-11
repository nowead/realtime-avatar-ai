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
using grpc::ClientWriterInterface;
using grpc::Status;
using tts::TTSService;
using tts::TTSStreamRequest;
using tts::SynthesisConfig;

class TTSClient {
public:
    explicit TTSClient(std::shared_ptr<Channel> channel);
    explicit TTSClient(std::shared_ptr<TTSService::StubInterface> stub);

    bool StartStream(const tts::SynthesisConfig& config);
    bool SendTextChunk(const std::string& text);
    Status FinishStream();
    bool IsStreamActive() const;

private:
    std::shared_ptr<TTSService::StubInterface> stub_;
    std::string server_address_;
    std::string session_id_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<grpc::ClientContext> context_;
    std::unique_ptr<ClientWriterInterface<TTSStreamRequest>> stream_;
    google::protobuf::Empty server_response_;
    mutable std::mutex stream_mutex_;
    std::atomic<bool> stream_active_{false};
};

} // namespace llm_engine

#endif // TTS_CLIENT_H