#pragma once
#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <vector>
#include <grpcpp/grpcpp.h>
#include "avatar_sync.grpc.pb.h" // 생성된 avatar_sync proto 헤더
#include <google/protobuf/empty.pb.h>

namespace tts {

// 네임스페이스 명시
using avatar_sync::AvatarSyncService;
using avatar_sync::AvatarSyncStreamRequest;
using avatar_sync::SyncConfig;
using avatar_sync::VisemeData; // VisemeData 타입 사용
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientWriter;
using grpc::Status;

class AvatarSyncClient {
public:
    explicit AvatarSyncClient(const std::string& server_address);
    ~AvatarSyncClient();

    AvatarSyncClient(const AvatarSyncClient&) = delete;
    AvatarSyncClient& operator=(const AvatarSyncClient&) = delete;
    AvatarSyncClient(AvatarSyncClient&&) = default;
    AvatarSyncClient& operator=(AvatarSyncClient&&) = default;

    // Avatar Sync 서비스로 스트림 시작 및 초기 설정(SyncConfig) 전송
    bool StartStream(const std::string& session_id);

    // 생성된 오디오 청크를 Avatar Sync 서비스로 전송
    bool SendAudioChunk(const std::vector<uint8_t>& audio_chunk);

    // (선택적) 생성된 비정형 데이터(VisemeData)를 Avatar Sync 서비스로 전송
    bool SendVisemeData(const VisemeData& viseme_data); // 단일 VisemeData 전송
    bool SendVisemeDataBatch(const std::vector<VisemeData>& visemes); // 여러 VisemeData 일괄 전송

    // 스트림 종료 및 최종 상태 수신
    Status FinishStream();

    // 현재 스트림이 활성 상태인지 확인
    bool IsStreamActive() const;

private:
    std::string server_address_;
    std::string session_id_; // 현재 활성화된 스트림의 세션 ID

    std::shared_ptr<Channel> channel_;
    std::unique_ptr<AvatarSyncService::Stub> stub_;

    // 스트리밍 RPC를 위한 멤버 변수
    std::unique_ptr<ClientContext> context_;
    std::unique_ptr<ClientWriter<AvatarSyncStreamRequest>> stream_;
    google::protobuf::Empty server_response_; // AvatarSync 서버의 응답은 Empty

    mutable std::mutex stream_mutex_;       // 스트림 관련 멤버 접근 보호
    std::atomic<bool> stream_active_{false}; // 스트림 활성 상태 플래그
};

} // namespace tts