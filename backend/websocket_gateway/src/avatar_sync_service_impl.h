// src/avatar_sync_service_impl.h
#ifndef AVATAR_SYNC_SERVICE_IMPL_H
#define AVATAR_SYNC_SERVICE_IMPL_H

#include <grpcpp/grpcpp.h>
#include "avatar_sync.grpc.pb.h"    // 생성된 proto 헤더
#include <functional>
#include <string>

#include "websocket_server.h" 
// types.h는 websocket_server.h를 통해 간접적으로 포함될 것으로 예상되지만,
// 명시적으로 필요한 경우 여기에 추가할 수 있습니다. (현재는 websocket_server.h에 의존)

namespace websocket_gateway { 

// AvatarSyncService의 gRPC 서비스 구현
class AvatarSyncServiceImpl final : public avatar_sync::AvatarSyncService::Service {
public:
    // WebSocketServer에 정의된 WebSocketConnection 타입을 사용 (네임스페이스 포함)
    // 이 using 선언이 성공하려면 websocket_gateway::WebSocketServer가 이 시점에서 완전한 타입이어야 합니다.
    using ConsistentWebSocketConnection = websocket_gateway::WebSocketServer::WebSocketConnection;
    
    // session_id로 WebSocket 연결을 찾아주는 콜백 타입 정의
    using WebSocketFinder = std::function< ConsistentWebSocketConnection* (const std::string& session_id) >;

    // 생성자: WebSocketFinder 콜백을 주입받음
    explicit AvatarSyncServiceImpl(WebSocketFinder finder);

    // gRPC 서비스 메소드 오버라이드
    grpc::Status SyncAvatarStream(
        grpc::ServerContext* context,
        grpc::ServerReader<avatar_sync::AvatarSyncStreamRequest>* reader,
        google::protobuf::Empty* response
    ) override;

private:
    WebSocketFinder find_websocket_by_session_id_; // 웹소켓 연결을 찾는 함수 포인터
};

} // namespace websocket_gateway

#endif // AVATAR_SYNC_SERVICE_IMPL_H
