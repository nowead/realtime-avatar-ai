// src/main.cpp
#include "websocket_server.h" 
#include "avatar_sync_service_impl.h" 
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <iostream>
#include <string>
#include <thread>
#include <csignal> 
#include <memory>  

// Environment variable names
const char* ENV_STT_SERVICE_ADDR = "STT_SERVICE_ADDR";
const char* ENV_GRPC_AVATAR_SYNC_ADDR = "GRPC_AVATAR_SYNC_ADDR";
const char* ENV_WS_PORT = "WS_PORT";
const char* ENV_METRICS_PORT = "METRICS_PORT";

// Default values
std::string STT_SERVICE_ADDR_DEFAULT = "stt-service:50052"; // Docker-compose 서비스 이름 사용
std::string GRPC_AVATAR_SYNC_ADDR_DEFAULT = "0.0.0.0:50055";
int WS_PORT_DEFAULT = 8000;
int METRICS_PORT_DEFAULT = 9090;

// ★ 네임스페이스를 사용하여 전역 변수 선언
std::unique_ptr<grpc::Server> grpc_server_instance;
std::unique_ptr<websocket_gateway::WebSocketServer> g_websocket_server_instance; // 네임스페이스 명시

void signal_handler(int signal) {
    std::cout << "\nCaught signal " << signal << ". Initiating graceful shutdown..." << std::endl;
    if (g_websocket_server_instance) { // ★ 수정된 전역 변수 사용
        std::cout << "Requesting WebSocket server to stop..." << std::endl;
        g_websocket_server_instance->stop(); 
    }
    if (grpc_server_instance) {
        std::cout << "Requesting gRPC server to shutdown..." << std::endl;
        // gRPC 서버 종료는 별도 스레드에서 호출하거나 데드라인을 사용하는 것이 더 안전할 수 있습니다.
        // 여기서는 main 스레드가 WebSocket 서버 종료 후 gRPC 스레드를 join 하므로,
        // gRPC 서버의 Wait()가 반환되도록 Shutdown()을 호출합니다.
        grpc_server_instance->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(5)); // 5초 데드라인
        // grpc_server_instance->Shutdown(); // 즉시 종료 요청
    }
}

// ★ AvatarSyncServiceImpl도 네임스페이스 명시
void RunGrpcServer(const std::string& grpc_addr, websocket_gateway::AvatarSyncServiceImpl* avatar_service) {
    grpc::ServerBuilder builder;
    builder.AddListeningPort(grpc_addr, grpc::InsecureServerCredentials());
    builder.RegisterService(avatar_service);

    grpc_server_instance = builder.BuildAndStart();
    if (grpc_server_instance) {
        std::cout << "gRPC AvatarSyncService listening on " << grpc_addr << std::endl;
        grpc_server_instance->Wait(); 
        std::cout << "gRPC AvatarSyncService has shut down." << std::endl;
    } else {
        std::cerr << "Failed to start gRPC AvatarSyncService on " << grpc_addr << std::endl;
        // gRPC 서버 시작 실패 시 전체 애플리케이션 종료 유도 가능
        if (g_websocket_server_instance) {
            g_websocket_server_instance->stop(); // WebSocket 서버도 중지 요청
        }
    }
}

int main(int argc, char** argv) {
    std::string stt_service_addr = std::getenv(ENV_STT_SERVICE_ADDR) ? std::getenv(ENV_STT_SERVICE_ADDR) : STT_SERVICE_ADDR_DEFAULT;
    std::string grpc_avatar_sync_addr = std::getenv(ENV_GRPC_AVATAR_SYNC_ADDR) ? std::getenv(ENV_GRPC_AVATAR_SYNC_ADDR) : GRPC_AVATAR_SYNC_ADDR_DEFAULT;
    int ws_port = std::getenv(ENV_WS_PORT) ? std::stoi(std::getenv(ENV_WS_PORT)) : WS_PORT_DEFAULT;
    int metrics_port = std::getenv(ENV_METRICS_PORT) ? std::stoi(std::getenv(ENV_METRICS_PORT)) : METRICS_PORT_DEFAULT;

    std::cout << "Configuration:" << std::endl;
    std::cout << " - WS_PORT: " << ws_port << std::endl;
    std::cout << " - METRICS_PORT: " << metrics_port << std::endl;
    std::cout << " - STT_SERVICE_ADDR: " << stt_service_addr << std::endl;
    std::cout << " - GRPC_AVATAR_SYNC_ADDR: " << grpc_avatar_sync_addr << std::endl;

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // ★ WebSocketServer 생성 시 네임스페이스 명시
    g_websocket_server_instance = std::make_unique<websocket_gateway::WebSocketServer>(ws_port, metrics_port, stt_service_addr);

    // ★ AvatarSyncServiceImpl 생성 및 WebSocketFinder 타입 명시
    // WebSocketFinder의 반환 타입이 websocket_gateway::WebSocketServer::WebSocketConnection* 이어야 함
    websocket_gateway::AvatarSyncServiceImpl::WebSocketFinder finder = 
        [&](const std::string& session_id) -> websocket_gateway::WebSocketServer::WebSocketConnection* {
        if (g_websocket_server_instance) { // ★ 수정된 전역 변수 사용
            return g_websocket_server_instance->find_websocket_by_session_id(session_id);
        }
        return nullptr;
    };
    // ★ AvatarSyncServiceImpl 생성 시 네임스페이스 명시
    websocket_gateway::AvatarSyncServiceImpl avatar_service(finder);

    std::thread grpc_thread(RunGrpcServer, grpc_avatar_sync_addr, &avatar_service);

    std::cout << "Starting WebSocket server..." << std::endl;
    if (!g_websocket_server_instance->run()) { // ★ 수정된 전역 변수 사용
        std::cerr << "Failed to run WebSocket server. Exiting." << std::endl;
        if (grpc_server_instance) {
            grpc_server_instance->Shutdown();
        }
        if (grpc_thread.joinable()) {
            grpc_thread.join();
        }
        return 1;
    }
    
    std::cout << "WebSocket server has stopped." << std::endl;

    if (grpc_server_instance && grpc_thread.joinable()) { // grpc_server_instance가 null이 아니고 스레드가 join 가능할 때
         std::cout << "Ensuring gRPC server shutdown and joining thread..." << std::endl;
         // Shutdown은 signal_handler 또는 RunGrpcServer 실패 시 이미 호출되었을 수 있음
         // grpc_server_instance->Shutdown(); // 필요시 중복 호출 방지 로직 추가
         grpc_thread.join();
         std::cout << "gRPC server thread joined." << std::endl;
    } else if (grpc_thread.joinable()) { // gRPC 서버 인스턴스는 없지만 스레드가 남아있는 경우 (예외적)
        std::cout << "gRPC server instance was null, but thread is joinable. Joining..." << std::endl;
        grpc_thread.join();
        std::cout << "gRPC server thread joined (instance was null)." << std::endl;
    }
    
    std::cout << "Application terminated gracefully." << std::endl;
    return 0;
}
