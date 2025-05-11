// src/main.cpp
#include "websocket_server.h" // New WebSocketServer class
#include "avatar_sync_service_impl.h" // AvatarSyncService implementation
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <iostream>
#include <string>
#include <thread>
#include <csignal> // For signal handling (graceful shutdown)
#include <memory>  // For std::unique_ptr

// Environment variable names (consider moving to a config header)
const char* ENV_STT_SERVICE_ADDR = "STT_SERVICE_ADDR";
const char* ENV_GRPC_AVATAR_SYNC_ADDR = "GRPC_AVATAR_SYNC_ADDR";
const char* ENV_WS_PORT = "WS_PORT";
const char* ENV_METRICS_PORT = "METRICS_PORT";

// Default values
std::string STT_SERVICE_ADDR_DEFAULT = "0.0.0.0:50051";
std::string GRPC_AVATAR_SYNC_ADDR_DEFAULT = "0.0.0.0:50052";
int WS_PORT_DEFAULT = 8080;
int METRICS_PORT_DEFAULT = 9090; // Can be same as WS_PORT if handled by same uWS App instance

std::unique_ptr<grpc::Server> grpc_server_instance;
std::unique_ptr<WebSocketServer> websocket_server_instance; // Global for signal handler

void signal_handler(int signal) {
    std::cout << "\nCaught signal " << signal << ". Initiating graceful shutdown..." << std::endl;
    if (websocket_server_instance) {
        std::cout << "Requesting WebSocket server to stop..." << std::endl;
        websocket_server_instance->stop(); // This should ideally unblock its run() method
    }
    if (grpc_server_instance) {
        std::cout << "Requesting gRPC server to shutdown..." << std::endl;
        // It's better if grpc_server_instance->Shutdown() is called from a different thread
        // than the one running grpc_server_instance->Wait() or if a deadline is used.
        // For simplicity here, we rely on the gRPC thread exiting after Wait() if triggered by external event or after WebSocket server stops.
        // A more robust approach would be to call Shutdown from here and then join the gRPC thread.
        // Or, AvatarSyncServiceImpl could detect server shutdown and stop processing.
         grpc_server_instance->Shutdown(); // Request shutdown
    }
    // The actual joining of threads and full cleanup will happen in main().
}


void RunGrpcServer(const std::string& grpc_addr, AvatarSyncServiceImpl* avatar_service) {
    grpc::ServerBuilder builder;
    builder.AddListeningPort(grpc_addr, grpc::InsecureServerCredentials());
    builder.RegisterService(avatar_service);

    grpc_server_instance = builder.BuildAndStart();
    if (grpc_server_instance) {
        std::cout << "gRPC AvatarSyncService listening on " << grpc_addr << std::endl;
        grpc_server_instance->Wait(); // Blocks until server is shutdown
        std::cout << "gRPC AvatarSyncService has shut down." << std::endl;
    } else {
        std::cerr << "Failed to start gRPC AvatarSyncService on " << grpc_addr << std::endl;
    }
}

int main(int argc, char** argv) {
    // Read configuration from environment variables
    std::string stt_service_addr = std::getenv(ENV_STT_SERVICE_ADDR) ? std::getenv(ENV_STT_SERVICE_ADDR) : STT_SERVICE_ADDR_DEFAULT;
    std::string grpc_avatar_sync_addr = std::getenv(ENV_GRPC_AVATAR_SYNC_ADDR) ? std::getenv(ENV_GRPC_AVATAR_SYNC_ADDR) : GRPC_AVATAR_SYNC_ADDR_DEFAULT;
    int ws_port = std::getenv(ENV_WS_PORT) ? std::stoi(std::getenv(ENV_WS_PORT)) : WS_PORT_DEFAULT;
    int metrics_port = std::getenv(ENV_METRICS_PORT) ? std::stoi(std::getenv(ENV_METRICS_PORT)) : METRICS_PORT_DEFAULT;

    std::cout << "Configuration:" << std::endl;
    std::cout << " - WS_PORT: " << ws_port << std::endl;
    std::cout << " - METRICS_PORT: " << metrics_port << std::endl;
    std::cout << " - STT_SERVICE_ADDR: " << stt_service_addr << std::endl;
    std::cout << " - GRPC_AVATAR_SYNC_ADDR: " << grpc_avatar_sync_addr << std::endl;

    // Setup signal handling for graceful shutdown
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Create WebSocketServer instance
    websocket_server_instance = std::make_unique<WebSocketServer>(ws_port, metrics_port, stt_service_addr);

    // Create AvatarSyncService implementation, providing the WebSocket finder
    // The lambda captures websocket_server_instance by pointer. Ensure lifetime.
    // Using a raw pointer capture for the lambda is okay here because AvatarSyncServiceImpl
    // will be managed by the gRPC server, which is joined before websocket_server_instance goes out of scope.
    AvatarSyncServiceImpl::WebSocketFinder finder = 
        [&](const std::string& session_id) -> AvatarSyncServiceImpl::ConsistentWebSocketConnection* {
        if (websocket_server_instance) {
            return websocket_server_instance->find_websocket_by_session_id(session_id);
        }
        return nullptr;
    };
    AvatarSyncServiceImpl avatar_service(finder);

    // Start gRPC server in a separate thread
    std::thread grpc_thread(RunGrpcServer, grpc_avatar_sync_addr, &avatar_service);

    // Run WebSocket server (this will block on the main thread)
    std::cout << "Starting WebSocket server..." << std::endl;
    if (!websocket_server_instance->run()) {
        std::cerr << "Failed to run WebSocket server. Exiting." << std::endl;
        // Ensure gRPC server is also stopped if WebSocket server fails to start
        if (grpc_server_instance) {
            grpc_server_instance->Shutdown();
        }
        if (grpc_thread.joinable()) {
            grpc_thread.join();
        }
        return 1;
    }
    // websocket_server_instance->run() has returned, meaning it was stopped.

    std::cout << "WebSocket server has stopped." << std::endl;

    // Ensure gRPC server is shut down and thread is joined
    // If not already shut down by signal handler, this ensures it.
    if (grpc_server_instance) {
         std::cout << "Ensuring gRPC server shutdown..." << std::endl;
         grpc_server_instance->Shutdown();
    }
    if (grpc_thread.joinable()) {
        std::cout << "Joining gRPC server thread..." << std::endl;
        grpc_thread.join();
        std::cout << "gRPC server thread joined." << std::endl;
    }
    
    // websocket_server_instance will be destroyed when main exits due to unique_ptr

    std::cout << "Application terminated gracefully." << std::endl;
    return 0;
}