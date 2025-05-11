#pragma once // 헤더 가드 추가

#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <vector>
#include <grpcpp/grpcpp.h>
#include "avatar_sync.grpc.pb.h" // 생성된 avatar_sync proto 헤더
#include <google/protobuf/empty.pb.h>

namespace tts {

// 네임스페이스 명시 (avatar_sync.proto에서 정의된 메시지 사용)
using avatar_sync::AvatarSyncService;
using avatar_sync::AvatarSyncStreamRequest;
using avatar_sync::SyncConfig; // ★ SyncConfig 사용
using avatar_sync::VisemeData;
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientWriter; // ClientWriterInterface 대신 ClientWriter 사용 (일반적)
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
    // ★ 인자를 const avatar_sync::SyncConfig& config 로 변경
    bool StartStream(const avatar_sync::SyncConfig& config);

    // 생성된 오디오 청크를 Avatar Sync 서비스로 전송
    bool SendAudioChunk(const std::vector<uint8_t>& audio_chunk);

    // 생성된 비정형 데이터(VisemeData)를 Avatar Sync 서비스로 전송
    bool SendVisemeData(const VisemeData& viseme_data);
    bool SendVisemeDataBatch(const std::vector<VisemeData>& visemes);

    // 스트림 종료 및 최종 상태 수신
    Status FinishStream();

    // 현재 스트림이 활성 상태인지 확인
    bool IsStreamActive() const;

private:
    std::string server_address_;
    std::string current_frontend_session_id_; // 현재 활성화된 스트림의 프론트엔드 세션 ID 저장용

    std::shared_ptr<Channel> channel_;
    std::unique_ptr<AvatarSyncService::Stub> stub_;

    std::unique_ptr<ClientContext> context_;
    std::unique_ptr<ClientWriter<AvatarSyncStreamRequest>> stream_;
    google::protobuf::Empty server_response_;

    mutable std::mutex stream_mutex_;
    std::atomic<bool> stream_active_{false};
};

} // namespace tts
