#pragma once

#include <string>
#include <memory> // std::shared_ptr

// gRPC 관련 헤더
#include <grpcpp/grpcpp.h>
#include "stt.grpc.pb.h" // 생성된 stt proto 헤더 (수정된 버전 기준)
#include "google/protobuf/empty.proto.h" // Empty 메시지 사용

// 내부 클라이언트 헤더
#include "azure_stt_client.h"
#include "llm_engine_client.h"

namespace stt {

// 네임스페이스 명시
using grpc::ServerContext;
using grpc::ServerReader; // Client Streaming 사용
using grpc::Status;
using google::protobuf::Empty;

// stt.proto에 정의된 STTService 구현 클래스
class STTServiceImpl final : public STTService::Service {
public:
    // 생성자: Azure 클라이언트와 LLM 클라이언트의 shared_ptr를 주입받습니다.
    // main 함수에서 이 클라이언트들을 생성하여 전달합니다.
    STTServiceImpl(std::shared_ptr<AzureSTTClient> azure_client,
                   std::shared_ptr<LLMEngineClient> llm_client);

    // Client Streaming RPC 구현 메소드
    Status RecognizeStream(
        ServerContext* context,
        ServerReader<STTStreamRequest>* reader, // 클라이언트로부터 요청 스트림 읽기
        Empty* response // 서버의 최종 단일 응답 (Empty)
    ) override;

private:
    std::shared_ptr<AzureSTTClient> azure_stt_client_;
    std::shared_ptr<LLMEngineClient> llm_engine_client_;
};

} // namespace stt