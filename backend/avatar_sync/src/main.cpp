#include "avatar.grpc.pb.h"
#include "avatar_sync_service.h"
#include "websocket_dispatcher_uwebs.h"
#include <nlohmann/json.hpp>
#include <grpcpp/grpcpp.h>
#include <uwebsockets/App.h>
#include <thread>
#include <iostream>

struct PerSocketData {
    std::string session_id;
};

void runWebSocketServer(std::shared_ptr<WebSocketDispatcherUWS> dispatcher) {
    uWS::App().ws<PerSocketData>("/*", {
        .open = [](auto* ws) {
            std::cout << "👋 WebSocket connected" << std::endl;
        },
        .message = [dispatcher](auto* ws, std::string_view msg, uWS::OpCode opCode) {
            try {
                auto j = nlohmann::json::parse(msg);
                if (j["type"] == "register") {
                    std::string session_id = j["session_id"];
                    ws->getUserData()->session_id = session_id;
                    dispatcher->registerSession(session_id, std::shared_ptr<void>(ws, [](void*){}));
                }
            } catch (...) {
                std::cerr << "⚠️ Invalid message" << std::endl;
            }
        },
        .close = [dispatcher](auto* ws, int, std::string_view) {
            auto session_id = ws->getUserData()->session_id;
            dispatcher->unregisterSession(session_id);
        }
    }).listen(9001, [](auto* token) {
        if (token) std::cout << "🚀 WebSocket server listening on :9001" << std::endl;
        else std::cerr << "❌ Failed to start WebSocket server" << std::endl;
    }).run();
}

int main() {
    std::string address("0.0.0.0:50056");

    // dispatcher를 공유 인스턴스로 생성
    auto dispatcher = std::make_shared<WebSocketDispatcherUWS>();

    // WebSocket 서버는 별도 스레드에서 실행
    std::thread ws_thread(runWebSocketServer, dispatcher);

    // gRPC 서버 초기화
    AvatarSyncServiceImpl service(dispatcher);

    grpc::ServerBuilder builder;
    builder.AddListeningPort(address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "✅ AvatarSync gRPC server listening on " << address << std::endl;

    // main 스레드는 gRPC wait
    server->Wait();

    // gRPC 종료되면 WebSocket 서버도 같이 종료
    ws_thread.join();

    return 0;
}
