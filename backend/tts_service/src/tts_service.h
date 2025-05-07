#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <functional> // For std::function

#include <grpcpp/grpcpp.h>
#include "tts.grpc.pb.h" // 생성된 tts proto 헤더
#include <google/protobuf/empty.pb.h>

#include "avatar_sync_client.h"
#include "azure_tts_engine.h"   // AzureTTSEngine 헤더 포함

namespace tts {

// 네임스페이스 명시
using grpc::ServerContext;
using grpc::ServerReader; // Client Streaming
using grpc::Status;
using google::protobuf::Empty;
// TTS 서비스 proto 메시지
using ::tts::TTSService;
using ::tts::TTSStreamRequest;
using ::tts::SynthesisConfig;


class TTSServiceImpl final : public TTSService::Service {
public:
    // 생성자: AvatarSync 클라이언트와 TTS 엔진 팩토리(또는 인스턴스) 주입
    explicit TTSServiceImpl(std::shared_ptr<AvatarSyncClient> avatar_sync_client,
                              std::function<std::unique_ptr<AzureTTSEngine>()> tts_engine_factory);
    ~TTSServiceImpl();

    // Client Streaming RPC: LLM으로부터 텍스트 스트림을 받아 음성 합성 후 AvatarSync로 스트리밍
    Status SynthesizeStream(
        ServerContext* context,
        ServerReader<TTSStreamRequest>* reader,
        Empty* response
    ) override;

private:
    std::shared_ptr<AvatarSyncClient> avatar_sync_client_;
    std::function<std::unique_ptr<AzureTTSEngine>()> tts_engine_factory_;

    // 간단한 UUID 생성 함수 (내부 헬퍼)
    static std::string generate_uuid();

    // 세션별 TTS 엔진 인스턴스 관리 (필요하다면)
    // mutable std::mutex sessions_mutex_;
    // std::unordered_map<std::string, std::unique_ptr<AzureTTSEngine>> active_tts_sessions_;
};

} // namespace tts