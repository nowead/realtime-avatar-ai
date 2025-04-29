#include "avatar.grpc.pb.h"
#include "avatar_sync_service.h"
#include "websocket_dispatcher.h"


AvatarSyncServiceImpl::AvatarSyncServiceImpl(std::shared_ptr<WebSocketDispatcher> dispatcher)
    : dispatcher_(dispatcher) {}

grpc::Status AvatarSyncServiceImpl::SyncAvatar(grpc::ServerContext*,
                                               const avatar::SyncRequest* request,
                                               avatar::SyncResponse* response) {
    std::string session_id = request->session_id();
    std::cout << "🔁 SyncAvatar called for session: " << session_id << std::endl;

    // 전달할 데이터 구성
    std::vector<uint8_t> audio(request->audio_data().begin(), request->audio_data().end());
    std::vector<avatar::Viseme> viseme_events(request->visemes().begin(), request->visemes().end());

    // WebSocket push
    if (!dispatcher_->sendToClient(session_id, audio, viseme_events, request->format())) {
        response->set_success(false);
        response->set_message("❌ Failed to send to client.");
        return grpc::Status::OK;
    }

    response->set_success(true);
    response->set_message("✅ Sent successfully");
    return grpc::Status::OK;
}
