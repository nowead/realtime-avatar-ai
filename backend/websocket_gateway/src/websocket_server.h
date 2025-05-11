// src/websocket_server.h
#ifndef WEBSOCKET_SERVER_H
#define WEBSOCKET_SERVER_H

#include <App.h>
#include <WebSocket.h> // For uWS::WebSocket, uWS::OpCode
#include <string>
#include <functional>
#include <map>
#include <mutex>
#include <atomic>
#include <memory>       // For std::unique_ptr
#include "types.h"      // For PerSocketData
#include "stt.pb.h"     // For stt::RecognitionConfig

// Forward declaration
class STTClient; // STTClient is used within PerSocketData

// Define SSL and Compression settings consistently
// These settings will be used by both WebSocketServer and AvatarSyncServiceImpl's WebSocketFinder type.
constexpr bool GLOBAL_SSL_ENABLED = false;
// Determine if compression is effectively enabled (true if not uWS::DISABLED)
constexpr uWS::CompressOptions GLOBAL_COMPRESSION_OPTIONS = uWS::SHARED_COMPRESSOR; // e.g., uWS::SHARED_COMPRESSOR, uWS::DEDICATED_COMPRESSOR_4KB etc. or uWS::DISABLED
constexpr bool GLOBAL_COMPRESSION_ACTUALLY_ENABLED = (GLOBAL_COMPRESSION_OPTIONS != uWS::DISABLED);


namespace uWS {
    // Forward declare for clarity if not fully brought in by App.h
    struct HttpRequest;
    template <bool SSL> struct HttpResponse;
}

class WebSocketServer {
public:
    // Define the WebSocket type using the global settings
    using WebSocketConnection = uWS::WebSocket<GLOBAL_SSL_ENABLED, GLOBAL_COMPRESSION_ACTUALLY_ENABLED, PerSocketData>;

    WebSocketServer(int ws_port, int metrics_port, const std::string& stt_service_addr);
    ~WebSocketServer();

    bool run();
    void stop();

    // Public method for AvatarSyncServiceImpl to find a WebSocket connection
    WebSocketConnection* find_websocket_by_session_id(const std::string& session_id);

private:
    void initialize_handlers();
    std::string generate_session_id();

    // WebSocket event handlers
    void on_websocket_open(WebSocketConnection* ws);
    void on_websocket_message(WebSocketConnection* ws, std::string_view message, uWS::OpCode op_code);
    void on_websocket_close(WebSocketConnection* ws, int code, std::string_view message);
    // void on_websocket_drain(WebSocketConnection* ws); // Example if needed
    // void on_websocket_ping(WebSocketConnection* ws, std::string_view message); // Example if needed
    // void on_websocket_pong(WebSocketConnection* ws, std::string_view message); // Example if needed

    // HTTP route handlers
    void handle_health_check(uWS::HttpResponse<GLOBAL_SSL_ENABLED>* res, uWS::HttpRequest* req);
    void handle_metrics(uWS::HttpResponse<GLOBAL_SSL_ENABLED>* res, uWS::HttpRequest* req);

    int ws_port_;
    int metrics_port_; // Can be the same as ws_port_
    std::string stt_service_address_;

    uWS::TemplatedApp<GLOBAL_SSL_ENABLED> app_;

    std::map<std::string, WebSocketConnection*> active_websockets_;
    std::mutex active_websockets_mutex_;

    std::atomic<long> connected_clients_count_{0};
    std::atomic<long> total_audio_bytes_processed_stt_{0};
    
    struct us_listen_socket_t *listen_socket_ws_ = nullptr;
    std::atomic<bool> is_shutting_down_{false};
};

#endif // WEBSOCKET_SERVER_H