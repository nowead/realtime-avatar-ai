#pragma once

#include <string>
#include <memory> // std::shared_ptr

#include <grpcpp/grpcpp.h>
// !! Proto 파일/메시지 이름 변경 시 아래 include 및 using 구문 확인 !!
#include "stt.grpc.pb.h" // 생성된 stt proto 헤더

#include <google/protobuf/empty.pb.h> // Empty 메시지 사용
// 내부 클라이언트 헤더
#include "azure_stt_client.h"
#include "llm_engine_client.h"

namespace stt {

// 네임스페이스 명시
using grpc::ServerContext;
using grpc::ServerReader; // Client Streaming 사용
using grpc::Status;
using google::protobuf::Empty;
// !! Proto 서비스/메시지 이름 변경 시 아래 using 구문 확인 !!
using ::stt::STTService;
using ::stt::STTStreamRequest;
// using ::stt::RecognitionConfig; // 필요한 경우
// using ::stt::AudioChunk;       // 필요한 경우

// stt.proto에 정의된 STTService 구현 클래스
class STTServiceImpl final : public STTService::Service {
public:
    // 생성자: 의존성 주입 (Azure 클라이언트, LLM 클라이언트)
    STTServiceImpl(std::shared_ptr<AzureSTTClient> azure_client,
                   std::shared_ptr<LLMEngineClient> llm_client);

    // Client Streaming RPC 구현 메소드
    // 클라이언트가 오디오 스트림을 보내면, 서버는 이를 처리하고 최종적으로 Empty 응답 반환
    // !! Proto 서비스/메소드 시그니처 변경 시 이 함수 시그니처 수정 필요 !!
    Status RecognizeStream(
        ServerContext* context,
        ServerReader<STTStreamRequest>* reader, // 클라이언트 요청 스트림
        Empty* response // 최종 응답 (내용 없음)
    ) override;

private:
    std::shared_ptr<AzureSTTClient> azure_stt_client_;
    std::shared_ptr<LLMEngineClient> llm_engine_client_;

    // 간단한 UUID 생성 함수 (내부 헬퍼)
    static std::string generate_uuid();
};

} // namespace stt