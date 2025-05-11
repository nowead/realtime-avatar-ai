#pragma once

#include <string>
#include <memory>
#include <vector>
#include <atomic>
#include <mutex>
#include <future>

#include <grpcpp/grpcpp.h>
#include <google/protobuf/empty.pb.h>
#include "llm.grpc.pb.h"

#include "tts_client.h"
#include "openai_client.h"

namespace llm_engine {

using grpc::ServerContext;
using grpc::ServerReader;
using grpc::Status;
using google::protobuf::Empty;
using llm::LLMService;
using llm::LLMStreamRequest;
using llm::SessionConfig;

class LLMServiceImpl final : public LLMService::Service {
public:
    LLMServiceImpl(std::shared_ptr<TTSClient> tts_client,
                   std::shared_ptr<OpenAIClient> openai_client);

    ~LLMServiceImpl() override = default;

    Status ProcessTextStream(
        ServerContext* context,
        ServerReader<LLMStreamRequest>* reader,
        Empty* response
    ) override;

private:
    std::shared_ptr<TTSClient> tts_client_;
    std::shared_ptr<OpenAIClient> openai_client_;

    static std::string generate_uuid();

    void handle_openai_chunk(const std::string& session_id, const std::string& chunk, std::atomic<bool>& tts_stream_ok);

    void handle_openai_completion(
        const std::string& session_id,
        bool success,
        const std::string& error_message,
        std::promise<void>& openai_done_promise,
        std::atomic<bool>& overall_success,
        std::string& last_error
        );
};

} // namespace llm_engine