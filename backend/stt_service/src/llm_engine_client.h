#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <utility> // For std::pair
#include <atomic> // For std::atomic_bool

#include <grpcpp/grpcpp.h>
#include <google/protobuf/empty.pb.h> // 수정됨: Empty 메시지 사용을 위해 추가

// 생성된 llm proto 헤더
#include "llm.grpc.pb.h"

namespace stt {

// 네임스페이스 명시 (llm 사용, LLMStreamSummary 제거)
using llm::LLMService;
using llm::LLMStreamRequest;
// using llm::LLMStreamSummary; // 수정됨: 제거 (proto에 없음)
using llm::SessionConfig;     // 수정됨: 추가 (LLMStreamRequest 내 사용)
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientWriter;
using grpc::Status;

// LLM Engine으로 텍스트 청크를 스트리밍하는 클라이언트 클래스
class LLMEngineClient {
public:
    explicit LLMEngineClient(const std::string& server_address);
    ~LLMEngineClient();

    // 복사 방지
    LLMEngineClient(const LLMEngineClient&) = delete;
    LLMEngineClient& operator=(const LLMEngineClient&) = delete;
    LLMEngineClient(LLMEngineClient&&) = default;
    LLMEngineClient& operator=(LLMEngineClient&&) = default;

    bool StartStream(const llm::SessionConfig& config);

    bool SendTextChunk(const std::string& text);

    Status FinishStream();

    bool IsStreamActive() const;

private:
    std::string server_address_;
    std::string session_id_;

    std::shared_ptr<Channel> channel_;
    std::unique_ptr<LLMService::Stub> stub_;

    std::unique_ptr<ClientContext> context_;
    std::unique_ptr<ClientWriter<LLMStreamRequest>> stream_;
    google::protobuf::Empty server_response_;

    mutable std::mutex stream_mutex_;
    std::atomic<bool> stream_active_{false};
};

} // namespace stt