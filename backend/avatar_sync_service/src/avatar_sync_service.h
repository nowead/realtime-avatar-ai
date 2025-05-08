#ifndef AVATAR_SYNC_SERVICE_IMPL_H
#define AVATAR_SYNC_SERVICE_IMPL_H

#include "protos/avatar_sync.grpc.pb.h" // 생성된 gRPC 코드
#include "webrtc_handler.h"
#include <memory>
#include <string>
#include <google/protobuf/timestamp.pb.h>

namespace avatar_sync_service {

class AvatarSyncServiceImpl final : public avatar_sync::AvatarSyncService::Service {
public:
    explicit AvatarSyncServiceImpl(std::shared_ptr<WebRTCHandler> webrtc_handler);

    grpc::Status SyncAvatarStream(
        grpc::ServerContext* context,
        grpc::ServerReader<avatar_sync::AvatarSyncStreamRequest>* reader,
        google::protobuf::Empty* response) override;

private:
    std::shared_ptr<WebRTCHandler> webrtc_handler_;
    // 필요하다면 세션별 상태를 관리하는 멤버 추가
};

} // namespace avatar_sync_service

#endif // AVATAR_SYNC_SERVICE_IMPL_H