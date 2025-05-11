// src/websocket_server.cpp
#include "websocket_server.h"
#include "stt_client.h"       // For STTClient
#include <nlohmann/json.hpp>  // For JSON processing
#include <iostream>
#include <random>             // For session ID generation
#include <sstream>            // For session ID generation
#include <iomanip>            // For session ID generation
#include <Loop.h> // For uWS::Loop::get()->defer()

// Helper to convert string_view to string safely for logging, etc.
inline std::string svToString(std::string_view sv) {
    return std::string(sv.data(), sv.length());
}

WebSocketServer::WebSocketServer(int ws_port, int metrics_port, const std::string& stt_service_addr)
    : ws_port_(ws_port),
      metrics_port_(metrics_port),
      stt_service_address_(stt_service_addr),
      // SocketContextOptions{} 를 명시적으로 전달합니다.
      app_(uWS::SocketContextOptions{}) { // SSL 옵션은 TemplatedApp<SSL_ENABLED> 에서 SSL_ENABLED 로 제어됩니다.
                                          // SSL 관련 옵션 (key, cert 파일 등)은 SocketContextOptions 에 설정합니다.
    if constexpr (GLOBAL_SSL_ENABLED) {
        // app_ = uWS::TemplatedApp<true>(uWS::SocketContextOptions{
        //     .key_file_name = "misc/key.pem", // SSL 설정 예시
        //     .cert_file_name = "misc/cert.pem",
        //     // .passphrase = "1234"
        // });
        // 위와 같이 SSL 옵션을 설정해야 하지만, 현재 GLOBAL_SSL_ENABLED = false 이므로 이 부분은 실행되지 않음
        // 만약 true로 변경 시, 위와 같은 SSL 옵션 설정 필요
         std::cout << "WebSocketServer initialized WITH SSL (SSL options would be configured here)." << std::endl;
    } else {
        // app_는 이미 uWS::TemplatedApp<false>(uWS::SocketContextOptions{}) 로 초기화됨
        std::cout << "WebSocketServer initialized WITHOUT SSL." << std::endl;
    }
    std::cout << "Compression: " << (GLOBAL_COMPRESSION_ACTUALLY_ENABLED ? "Yes" : "No") << std::endl;
}

WebSocketServer::~WebSocketServer() {
    if (!is_shutting_down_.load()) {
        stop(); // Ensure graceful shutdown if not already initiated
    }
    std::cout << "WebSocketServer destroyed." << std::endl;
}

std::string WebSocketServer::generate_session_id() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> distrib;
    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << distrib(gen);
    ss << std::hex << std::setw(16) << std::setfill('0') << distrib(gen);
    return ss.str();
}

void WebSocketServer::initialize_handlers() {
    app_.ws<PerSocketData>("/*", {
        /* Settings */
        .compression = GLOBAL_COMPRESSION_OPTIONS,
        .maxPayloadLength = 16 * 1024 * 1024, // 16MB
        .idleTimeout = 60, // 60 seconds

        /* Handlers */
        .open = [this](WebSocketConnection *ws) { this->on_websocket_open(ws); },
        .message = [this](WebSocketConnection *ws, std::string_view message, uWS::OpCode op_code) { this->on_websocket_message(ws, message, op_code); },
        .close = [this](WebSocketConnection *ws, int code, std::string_view message) { this->on_websocket_close(ws, code, message); }
        // Add .drain, .ping, .pong if specific logic is needed
    });

    // HTTP Handlers
    app_.get("/healthz", [this](uWS::HttpResponse<GLOBAL_SSL_ENABLED>* res, uWS::HttpRequest* req) { this->handle_health_check(res, req); });
    app_.get("/metrics", [this](uWS::HttpResponse<GLOBAL_SSL_ENABLED>* res, uWS::HttpRequest* req) { this->handle_metrics(res, req); });
}

bool WebSocketServer::run() {
    initialize_handlers(); // Setup WebSocket and HTTP handlers

    bool success_ws = false;
    app_.listen(ws_port_, [this, &success_ws](auto *token) {
        this->listen_socket_ws_ = token;
        if (token) {
            std::cout << "WebSocket server listening on port " << ws_port_ << std::endl;
            success_ws = true;
        } else {
            std::cerr << "Failed to listen on WebSocket port " << ws_port_ << std::endl;
            success_ws = false;
        }
    });
    if (!success_ws) return false;

    // If metrics port is different and requires a separate listen call:
    if (metrics_port_ != ws_port_) {
        bool success_metrics = false;
        // Note: uWS typically runs one event loop per App. If you need truly separate ports
        // with potentially different configurations, you might need another App instance.
        // For simplicity, if metrics_port_ is different, this example assumes it implies
        // the same app instance is trying to listen on another port, which uWS might allow
        // or might require careful handling or a different App instance.
        // The original main.cpp had a second .listen() call on the same app for metrics.
        app_.listen(metrics_port_, [this, &success_metrics](auto* token){
            if (token) {
                std::cout << "Metrics HTTP server listening on port " << metrics_port_ << std::endl;
                success_metrics = true;
            } else {
                 std::cerr << "Failed to listen on metrics port " << metrics_port_ << ". Metrics might only be available on WS port if distinct listen fails." << std::endl;
                 success_metrics = false; // Or handle this as a non-fatal issue
            }
        });
        // if (!success_metrics) { /* handle error or log */ }
    }


    std::cout << "WebSocketServer starting event loop..." << std::endl;
    app_.run(); // This blocks until stop() is called or an issue occurs
    
    std::cout << "WebSocketServer event loop has ended." << std::endl;
    return true; // Or based on how app_.run() exits
}

void WebSocketServer::stop() {
    if (is_shutting_down_.exchange(true)) {
        return; // Shutdown already in progress
    }
    std::cout << "WebSocketServer: Initiating graceful shutdown..." << std::endl;

    if (uWS::Loop::get()) { // Ensure we are on the loop's thread or can defer to it
        uWS::Loop::get()->defer([this]() {
            std::cout << "WebSocketServer: Executing deferred shutdown tasks..." << std::endl;
            // Close all active WebSocket connections
            {
                std::lock_guard<std::mutex> lock(active_websockets_mutex_);
                std::cout << "WebSocketServer: Closing " << active_websockets_.size() << " active WebSocket connections..." << std::endl;
                for (auto const& [session_id, ws_ptr] : active_websockets_) {
                    if (ws_ptr) {
                        ws_ptr->end(1001, "Server shutting down"); // 1001: Going Away
                    }
                }
                active_websockets_.clear();
            }
            std::cout << "WebSocketServer: All WebSocket connections signaled to close." << std::endl;

            // Close the listen socket(s)
            if (listen_socket_ws_) {
                std::cout << "WebSocketServer: Closing listen socket on port " << ws_port_ << std::endl;
                // The parameter '0' indicates no SSL, adjust if SSL is used for the socket context
                us_listen_socket_close(GLOBAL_SSL_ENABLED ? 1: 0, listen_socket_ws_);
                listen_socket_ws_ = nullptr;
            }
            // If a separate listen_socket for metrics was stored, close it too.
            std::cout << "WebSocketServer: Shutdown tasks complete." << std::endl;
            // uWS::Loop::get()->stop(); // Or similar to break the loop if app.run() doesn't exit from closing sockets.
                                     // Often, closing listen sockets is enough for app.run() to return.
        });
    } else {
         std::cerr << "WebSocketServer: uWS::Loop not accessible for deferred shutdown. Manual cleanup might be incomplete." << std::endl;
    }
    // app_.stop() or similar might be available in newer uWS versions if just closing sockets isn't enough
}


WebSocketServer::WebSocketConnection* WebSocketServer::find_websocket_by_session_id(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(active_websockets_mutex_);
    auto it = active_websockets_.find(session_id);
    if (it != active_websockets_.end()) {
        return it->second;
    }
    return nullptr;
}

// --- WebSocket Handlers ---
void WebSocketServer::on_websocket_open(WebSocketConnection* ws) {
    connected_clients_count_++;
    PerSocketData *user_data = ws->getUserData(); // uWS manages allocation of PerSocketData

    user_data->sessionId = generate_session_id();
    user_data->stt_client = std::make_unique<STTClient>(stt_service_address_);
    user_data->stt_stream_active = false;

    {
        std::lock_guard<std::mutex> lock(active_websockets_mutex_);
        active_websockets_[user_data->sessionId] = ws;
    }

    std::cout << "[" << user_data->sessionId << "] WebSocket client connected from "
              << svToString(ws->getRemoteAddressAsText()) // Ensure string_view is handled
              << ". Total clients: " << connected_clients_count_.load() << std::endl;

    // Send session_id to the client
    nlohmann::json session_info = {{"type", "session_info"}, {"sessionId", user_data->sessionId}};
    ws->send(session_info.dump(), uWS::OpCode::TEXT);
}

void WebSocketServer::on_websocket_message(WebSocketConnection* ws, std::string_view message, uWS::OpCode op_code) {
    PerSocketData *user_data = ws->getUserData();
    // user_data->last_activity = std::chrono::steady_clock::now(); // If implementing idle timeout

    if (op_code == uWS::OpCode::TEXT) {
        std::cout << "[" << user_data->sessionId << "] Received Text message: " << svToString(message) << std::endl;
        try {
            nlohmann::json ctrl_msg = nlohmann::json::parse(message);
            if (ctrl_msg.contains("type")) {
                std::string type = ctrl_msg["type"];
                if (type == "start_stream") {
                    if (user_data->stt_stream_active) {
                        std::cerr << "[" << user_data->sessionId << "] STT stream already active. Ignoring." << std::endl;
                        return;
                    }
                    stt::RecognitionConfig stt_config;
                    stt_config.set_session_id(user_data->sessionId);
                    stt_config.set_language(ctrl_msg.value("language", "ko-KR")); // Default to ko-KR

                    std::cout << "[" << user_data->sessionId << "] Starting STT stream (Lang: " << stt_config.language() << ")." << std::endl;
                    bool started = user_data->stt_client->StartStream(stt_config,
                        [this, session_id = user_data->sessionId, ws_captured = ws](const grpc::Status& status) {
                            // STT stream finished callback
                            WebSocketConnection* current_ws = find_websocket_by_session_id(session_id);
                            if (current_ws && current_ws == ws_captured) { // Check if ws is still valid and the one we started with
                                PerSocketData* current_data = current_ws->getUserData();
                                current_data->stt_stream_active = false;
                                std::cout << "[" << session_id << "] STT stream finished with status: ("
                                          << status.error_code() << ") " << status.error_message() << std::endl;
                                if (!status.ok()) {
                                    nlohmann::json error_msg = {
                                        {"type", "error"}, {"source", "stt"},
                                        {"code", status.error_code()}, {"message", status.error_message()}
                                    };
                                    current_ws->send(error_msg.dump(), uWS::OpCode::TEXT);
                                }
                            } else {
                                std::cout << "[" << session_id << "] STT stream callback for an old/invalid WebSocket session." << std::endl;
                            }
                        });

                    if (started) {
                        user_data->stt_stream_active = true;
                    } else {
                        std::cerr << "[" << user_data->sessionId << "] Failed to start STT stream." << std::endl;
                        nlohmann::json error_msg = {{"type", "error"}, {"source", "stt_client"}, {"message", "Failed to start STT stream"}};
                        ws->send(error_msg.dump(), uWS::OpCode::TEXT);
                    }

                } else if (type == "stop_stream") {
                    if (!user_data->stt_stream_active) {
                        std::cout << "[" << user_data->sessionId << "] STT stream not active. Ignoring stop_stream." << std::endl;
                        return;
                    }
                    std::cout << "[" << user_data->sessionId << "] Stopping STT stream (WritesDone)." << std::endl;
                    user_data->stt_client->WritesDoneAndFinish();
                    // stt_stream_active will be set to false in the callback
                } else if (type == "heartbeat") {
                    nlohmann::json heartbeat_ack = {{"type", "heartbeat_ack"}};
                    ws->send(heartbeat_ack.dump(), uWS::OpCode::TEXT);
                }
            }
        } catch (const nlohmann::json::parse_error& e) {
            std::cerr << "[" << user_data->sessionId << "] JSON parse error: " << e.what() << std::endl;
        }

    } else if (op_code == uWS::OpCode::BINARY) {
        if (user_data->stt_stream_active && user_data->stt_client) {
            total_audio_bytes_processed_stt_ += message.length();
            if (!user_data->stt_client->WriteAudioChunk(std::string(message))) { // STTClient expects std::string
                 std::cerr << "[" << user_data->sessionId << "] Failed to write audio chunk to STTClient. Ending STT stream." << std::endl;
                 user_data->stt_client->WritesDoneAndFinish(); // Problem with stream, try to end it
            }
        } else {
            // std::cout << "[" << user_data->sessionId << "] Received Binary audio frame but STT stream not active. Discarding." << std::endl;
        }
    }
}

void WebSocketServer::on_websocket_close(WebSocketConnection* ws, int code, std::string_view message) {
    connected_clients_count_--;
    PerSocketData *user_data = ws->getUserData();
    std::string session_id_copy = user_data->sessionId; // Copy before potential userData invalidation (though uWS handles it)

    std::cout << "[" << session_id_copy << "] WebSocket client disconnected. Code: " << code << ", Msg: " << svToString(message)
              << ". Total clients: " << connected_clients_count_.load() << std::endl;

    if (user_data->stt_stream_active && user_data->stt_client) {
        std::cout << "[" << session_id_copy << "] Forcing STT stream stop due to WebSocket close." << std::endl;
        user_data->stt_client->StopStreamNow(); // Forcefully stop the STT stream
    }

    {
        std::lock_guard<std::mutex> lock(active_websockets_mutex_);
        active_websockets_.erase(session_id_copy);
    }
    // user_data->stt_client (unique_ptr) will be automatically destroyed when PerSocketData is cleaned up by uWS
}

// --- HTTP Handlers ---
void WebSocketServer::handle_health_check(uWS::HttpResponse<GLOBAL_SSL_ENABLED>* res, uWS::HttpRequest* req) {
    // TODO: Add checks for gRPC service (STT) connectivity if possible
    res->writeHeader("Content-Type", "text/plain")->end("OK");
}

void WebSocketServer::handle_metrics(uWS::HttpResponse<GLOBAL_SSL_ENABLED>* res, uWS::HttpRequest* req) {
    std::string metrics_data = "# HELP connected_clients WebSocket connected clients\n";
    metrics_data += "# TYPE connected_clients gauge\n";
    metrics_data += "connected_clients " + std::to_string(connected_clients_count_.load()) + "\n\n";

    metrics_data += "# HELP total_audio_bytes_processed_stt Total audio bytes processed by STT client\n";
    metrics_data += "# TYPE total_audio_bytes_processed_stt counter\n";
    metrics_data += "total_audio_bytes_processed_stt " + std::to_string(total_audio_bytes_processed_stt_.load()) + "\n";

    res->writeHeader("Content-Type", "text/plain; version=0.0.4")->end(metrics_data);
}