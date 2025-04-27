#pragma once

#include "avatar.pb.h"  // for avatar::SyncRequest, avatar::SyncResponse
#include "avatar.grpc.pb.h"
#include <memory>
#include <grpcpp/grpcpp.h> 

class WebSocketDispatcher;  // forward declaration

class AvatarSyncServiceImpl final : public avatar::AvatarSync::Service {
    public:
        explicit AvatarSyncServiceImpl(std::shared_ptr<WebSocketDispatcher> dispatcher);

        grpc::Status SyncAvatar(grpc::ServerContext* context,
                                const avatar::SyncRequest* request,
                                avatar::SyncResponse* response) override;

    private:
        std::shared_ptr<WebSocketDispatcher> dispatcher_;
};
