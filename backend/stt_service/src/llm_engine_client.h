#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <utility> // For std::pair
#include <atomic> // For std::atomic

// gRPC 관련 헤더
#include <grpcpp/grpcpp.h>

// 생성된 llm_engine proto 헤더 (빌드 후 생성됨)
// 'proto/llm_engine.proto' (Client Streaming 버전) 기준
#include "llm_engine.grpc.pb.h"

namespace stt {

// 네임스페이스 명시
using llm_engine::LLMService;
using llm_engine::LLMStreamRequest;
using llm_engine::LLMStreamSummary; // 응답은 단일 Summary 메시지
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientWriter; // Client Streaming 사용
using grpc::Status;

// LLM Engine으로 텍스트 청크를 스트리밍하는 클라이언트 클래스 (Client Streaming 방식)
class LLMEngineClient {
public:
    // 생성자: llm-engine 서버 주소 필요 (예: "llm-engine:50051")
    explicit LLMEngineClient(const std::string& server_address);
    ~LLMEngineClient();

    // 복사 방지 (이동은 가능)
    LLMEngineClient(const LLMEngineClient&) = delete;
    LLMEngineClient& operator=(const LLMEngineClient&) = delete;
    LLMEngineClient(LLMEngineClient&&) = default;
    LLMEngineClient& operator=(LLMEngineClient&&) = default;

    // llm-engine으로 gRPC 스트림 시작
    // parameters: (세션 식별 ID - 선택 사항)
    // returns: 스트림 시작 성공 여부
    bool StartStream(const std::string& session_id = "");

    // 인식된 텍스트 청크를 llm-engine으로 전송
    // parameters: (텍스트 내용, 최종 청크 여부)
    // returns: 전송 성공 여부 (스트림 유효 여부)
    bool SendTextChunk(const std::string& text, bool is_final);

    // 스트림 종료 처리 (쓰기 완료 및 최종 응답/상태 수신)
    // returns: gRPC 최종 상태와 서버로부터 받은 Summary 응답
    std::pair<Status, LLMStreamSummary> FinishStream();

    // 현재 스트림이 활성 상태인지 확인하는 헬퍼 함수
    bool IsStreamActive() const;

private:
    std::string server_address_;
    std::string session_id_;

    std::shared_ptr<Channel> channel_;
    std::unique_ptr<LLMService::Stub> stub_;

    // 스트림 관련 멤버 변수
    std::unique_ptr<ClientContext> context_; // unique_ptr 사용 권장
    std::unique_ptr<ClientWriter<LLMStreamRequest>> stream_; // ClientWriter 사용
    LLMStreamSummary summary_response_; // 서버의 최종 단일 응답을 받을 변수

    // 스트림 쓰기 보호용 뮤텍스
    std::mutex stream_mutex_;
    // 스트림 활성 상태 플래그 (atomic으로 스레드 안전성 확보)
    std::atomic<bool> stream_active_{false};
};

} // namespace stt