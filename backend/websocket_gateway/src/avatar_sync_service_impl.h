// src/avatar_sync_service_impl.h
#ifndef AVATAR_SYNC_SERVICE_IMPL_H
#define AVATAR_SYNC_SERVICE_IMPL_H

#include <grpcpp/grpcpp.h>
#include "avatar_sync.grpc.pb.h"    // avatar_sync.proto에서 생성됨
#include <functional>
#include <string>
#include "types.h"                  // PerSocketData 정의
#include "websocket_server.h"       // For GLOBAL_SSL_ENABLED, GLOBAL_COMPRESSION_ACTUALLY_ENABLED

// uWS::WebSocket forward declaration - use types from websocket_server.h for consistency
namespace uWS {
    template <bool SSL, bool COMPRESS, typename UserData>
    class WebSocket;
}

class AvatarSyncServiceImpl final : public avatar_sync::AvatarSyncService::Service {
public:
    // Use the WebSocket type defined in WebSocketServer for consistency
    // This ensures that the pointer type matches exactly.
    using ConsistentWebSocketConnection = uWS::WebSocket<GLOBAL_SSL_ENABLED, GLOBAL_COMPRESSION_ACTUALLY_ENABLED, PerSocketData>;
    
    // session_id로 WebSocket을 찾아주는 콜백 타입
    using WebSocketFinder = std::function< ConsistentWebSocketConnection* (const std::string& session_id) >;

    explicit AvatarSyncServiceImpl(WebSocketFinder finder);

    grpc::Status SyncAvatarStream(
        grpc::ServerContext* context,
        grpc::ServerReader<avatar_sync::AvatarSyncStreamRequest>* reader,
        google::protobuf::Empty* response
    ) override;

private:
    WebSocketFinder find_websocket_by_session_id_;
};

#endif // AVATAR_SYNC_SERVICE_IMPL_H