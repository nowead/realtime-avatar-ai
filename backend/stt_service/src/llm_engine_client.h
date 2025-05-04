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

    // llm-engine으로 gRPC 스트림 시작 및 초기 설정 전송
    // session_id: 이 스트림을 식별하는 ID
    // returns: 스트림 시작 및 초기 설정 전송 성공 여부
    bool StartStream(const std::string& session_id); // session_id 기본값 제거 (필수값으로 가정)

    // 인식된 텍스트 청크를 llm-engine으로 전송 (is_final 파라미터 제거됨)
    // text: 전송할 텍스트 조각
    // returns: 전송 성공 여부
    bool SendTextChunk(const std::string& text); // 수정됨: is_final 파라미터 제거

    // 스트림 종료 처리 (쓰기 완료 알림 및 최종 상태 수신)
    // 서버는 google.protobuf.Empty를 반환하므로 최종 상태(Status)만 반환
    // returns: gRPC 최종 상태
    Status FinishStream(); // 수정됨: 반환 타입 변경 (LLMStreamSummary 제거)

    // 현재 스트림이 활성 상태인지 확인
    bool IsStreamActive() const;

private:
    std::string server_address_;
    std::string session_id_;

    std::shared_ptr<Channel> channel_;
    std::unique_ptr<LLMService::Stub> stub_;

    std::unique_ptr<ClientContext> context_;
    std::unique_ptr<ClientWriter<LLMStreamRequest>> stream_;
    // LLMStreamSummary summary_response_; // 수정됨: 제거 (proto에 없음)
    google::protobuf::Empty server_response_; // 수정됨: 서버 응답 타입 변경

    mutable std::mutex stream_mutex_;
    std::atomic<bool> stream_active_{false};
};

} // namespace stt