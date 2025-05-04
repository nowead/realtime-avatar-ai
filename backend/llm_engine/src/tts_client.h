#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <atomic>

#include <grpcpp/grpcpp.h>
#include <google/protobuf/empty.pb.h>
#include "tts.grpc.pb.h" // Include generated TTS proto code

namespace llm_engine {

// Using declarations for convenience
using tts::TTSService;
using tts::TTSStreamRequest;
using tts::SynthesisConfig;
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientWriter;
using grpc::Status;

class TTSClient {
public:
    explicit TTSClient(const std::string& server_address);
    ~TTSClient();

    // Deleted copy/move constructors and assignment operators
    TTSClient(const TTSClient&) = delete;
    TTSClient& operator=(const TTSClient&) = delete;
    TTSClient(TTSClient&&) = delete;
    TTSClient& operator=(TTSClient&&) = delete;

    // Starts the gRPC stream to the TTS service
    bool StartStream(const std::string& session_id, const std::string& language_code, const std::string& voice_name);

    // Sends a text chunk to the TTS service
    bool SendTextChunk(const std::string& text);

    // Finishes the gRPC stream and returns the final status from TTS service
    Status FinishStream();

    // Checks if the stream is currently active
    bool IsStreamActive() const;

private:
    std::string server_address_;
    std::string session_id_; // Store session ID for logging/debugging

    std::shared_ptr<Channel> channel_;
    std::unique_ptr<TTSService::Stub> stub_;

    // Per-stream resources
    std::unique_ptr<ClientContext> context_;
    std::unique_ptr<ClientWriter<TTSStreamRequest>> stream_;
    google::protobuf::Empty server_response_; // TTS returns Empty

    mutable std::mutex stream_mutex_;
    std::atomic<bool> stream_active_{false};
};

} // namespace llm_engine