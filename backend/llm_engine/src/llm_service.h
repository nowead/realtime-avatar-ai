#pragma once

#include <string>
#include <memory>
#include <vector>
#include <atomic>
#include <mutex>
#include <future> // For promise/future

#include <grpcpp/grpcpp.h>
#include <google/protobuf/empty.pb.h>
#include "llm.grpc.pb.h" // Generated LLM proto code

#include "tts_client.h"     // Include our TTS client
#include "openai_client.h"  // Include our OpenAI client

namespace llm_engine {

using grpc::ServerContext;
using grpc::ServerReader;
using grpc::Status;
using google::protobuf::Empty;
using llm::LLMService;
using llm::LLMStreamRequest;
using llm::SessionConfig;

// Implementation of the LLMService defined in llm.proto
class LLMServiceImpl final : public LLMService::Service {
public:
    // Constructor injection for dependencies
    LLMServiceImpl(std::shared_ptr<TTSClient> tts_client,
                   std::shared_ptr<OpenAIClient> openai_client);

    ~LLMServiceImpl() override = default;

    // Override the ProcessTextStream method from the proto definition
    Status ProcessTextStream(
        ServerContext* context,
        ServerReader<LLMStreamRequest>* reader,
        Empty* response
    ) override;

private:
    std::shared_ptr<TTSClient> tts_client_;
    std::shared_ptr<OpenAIClient> openai_client_;

    // Helper to generate unique IDs (can reuse from stt_service if made common)
    static std::string generate_uuid();

    // Function to handle chunks received from OpenAI
    void handle_openai_chunk(const std::string& session_id, const std::string& chunk, std::atomic<bool>& tts_stream_ok);

    // Function to handle OpenAI stream completion/error
    void handle_openai_completion(
        const std::string& session_id,
        bool success,
        const std::string& error_message,
        std::promise<void>& openai_done_promise, // Promise to signal completion
        std::atomic<bool>& overall_success,      // Flag to track errors
        std::string& last_error                 // Store last error message
        );
};

} // namespace llm_engine