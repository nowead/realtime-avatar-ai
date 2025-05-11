// src/websocket_server.cpp
#include "websocket_server.h" 
#include "stt_client.h"       
#include <nlohmann/json.hpp>  
#include <iostream>
#include <random>             
#include <sstream>            
#include <iomanip>            
#include <Loop.h>             
#include "stt.pb.h"           

// svToString 헬퍼 함수 (이전과 동일)
inline std::string svToString(std::string_view sv) {
    if (sv.data() == nullptr) return "";
    return std::string(sv.data(), sv.length());
}
inline std::string svToString(const grpc::string_ref& sr) {
    if (sr.data() == nullptr) return "";
    return std::string(sr.data(), sr.length());
}
inline std::string svToString(const std::string& s) {
    return s;
}


namespace websocket_gateway {

WebSocketServer::WebSocketServer(int ws_port, int metrics_port, const std::string& stt_service_addr)
    : ws_port_(ws_port),
      metrics_port_(metrics_port),
      stt_service_address_(stt_service_addr),
      app_(uWS::SocketContextOptions{}) { 
    if constexpr (GLOBAL_SSL_ENABLED) {
         std::cout << "WebSocketServer initialized WITH SSL." << std::endl;
    } else {
        std::cout << "WebSocketServer initialized WITHOUT SSL." << std::endl;
    }
    std::cout << "Compression: " << (GLOBAL_COMPRESSION_ACTUALLY_ENABLED ? "Yes" : "No") << std::endl;
}

WebSocketServer::~WebSocketServer() {
    std::cout << "WebSocketServer destructor called. Shutting down status: " << is_shutting_down_.load() << std::endl;
    if (!is_shutting_down_.load()) {
        stop(); 
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
        .compression = GLOBAL_COMPRESSION_OPTIONS,
        .maxPayloadLength = 16 * 1024 * 1024, 
        .idleTimeout = 600, // 10분

        .open = [this](WebSocketConnection *ws) { this->on_websocket_open(ws); },
        .message = [this](WebSocketConnection *ws, std::string_view message, uWS::OpCode op_code) { this->on_websocket_message(ws, message, op_code); },
        .drain = [](WebSocketConnection *ws) { /* TODO: Implement if needed for backpressure */ },
        .ping = [](WebSocketConnection *ws, std::string_view) { /* Default uWS ping/pong handling is usually sufficient */ },
        .pong = [](WebSocketConnection *ws, std::string_view) { /* ... */ },
        .close = [this](WebSocketConnection *ws, int code, std::string_view message) { this->on_websocket_close(ws, code, message); }
    });

    app_.get("/healthz", [this](uWS::HttpResponse<GLOBAL_SSL_ENABLED>* res, uWS::HttpRequest* req) { this->handle_health_check(res, req); });
    app_.get("/metrics", [this](uWS::HttpResponse<GLOBAL_SSL_ENABLED>* res, uWS::HttpRequest* req) { this->handle_metrics(res, req); });
}

bool WebSocketServer::run() {
    initialize_handlers(); 

    bool success_ws = false;
    app_.listen(ws_port_, [this, &success_ws](auto *token) { 
        this->listen_socket_ws_ = token;
        if (token) {
            std::cout << "WebSocket server listening on port " << this->ws_port_ << std::endl;
            success_ws = true;
        } else {
            std::cerr << "Failed to listen on WebSocket port " << this->ws_port_ << std::endl;
            success_ws = false;
        }
    });
    if (!success_ws) return false;

    if (metrics_port_ > 0 && metrics_port_ != ws_port_) { 
        bool success_metrics = false;
        app_.listen(metrics_port_, [this, &success_metrics](auto* token){ 
            if (token) {
                std::cout << "Metrics HTTP server listening on port " << this->metrics_port_ << std::endl;
                success_metrics = true;
            } else {
                 // ★★★ 수정된 부분: this.metrics_port_ -> this->metrics_port_ ★★★
                 std::cerr << "Failed to listen on metrics port " << this->metrics_port_ << ". Metrics might only be available on WS port if distinct listen fails." << std::endl;
            }
        });
    }

    std::cout << "WebSocketServer starting event loop..." << std::endl;
    app_.run(); 
    std::cout << "WebSocketServer event loop has ended." << std::endl;
    return true; 
}

void WebSocketServer::stop() {
    if (is_shutting_down_.exchange(true)) { 
        return; 
    }
    std::cout << "WebSocketServer: Initiating graceful shutdown..." << std::endl;

    if (uWS::Loop::get()) { 
        uWS::Loop::get()->defer([this]() { 
            std::cout << "WebSocketServer: Executing deferred shutdown tasks on uWS loop thread..." << std::endl;
            
            {
                std::lock_guard<std::mutex> lock(active_websockets_mutex_);
                std::cout << "WebSocketServer: Closing " << active_websockets_.size() << " active WebSocket connections..." << std::endl;
                for (auto const& [session_id, ws_ptr] : active_websockets_) {
                    if (ws_ptr) {
                        PerSocketData* psd = ws_ptr->getUserData();
                        if (psd && psd->stt_client) {
                            std::cout << "  Shutting down STTClient for session: " << psd->sessionId << std::endl;
                            psd->stt_client->StopStreamNow(); 
                        }
                        ws_ptr->end(1001, "Server shutting down"); 
                    }
                }
                active_websockets_.clear(); 
            }
            std::cout << "WebSocketServer: All WebSocket connections signaled to close." << std::endl;

            if (listen_socket_ws_) {
                std::cout << "WebSocketServer: Closing listen socket on port " << ws_port_ << std::endl;
                us_listen_socket_close(GLOBAL_SSL_ENABLED ? 1 : 0, listen_socket_ws_);
                listen_socket_ws_ = nullptr;
            }
            std::cout << "WebSocketServer: Deferred shutdown tasks complete." << std::endl;
        });
    } else {
         std::cerr << "WebSocketServer: uWS::Loop not accessible for deferred shutdown." << std::endl;
    }
    std::cout << "WebSocketServer: stop() method finished." << std::endl;
}


WebSocketServer::WebSocketConnection* WebSocketServer::find_websocket_by_session_id(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(active_websockets_mutex_);
    auto it = active_websockets_.find(session_id);
    if (it != active_websockets_.end()) {
        return it->second;
    }
    return nullptr;
}

void WebSocketServer::on_websocket_open(WebSocketConnection* ws) {
    connected_clients_count_++; 
    PerSocketData *user_data = ws->getUserData(); 

    user_data->sessionId = generate_session_id();
    try {
        user_data->stt_client = std::make_unique<websocket_gateway::STTClient>(stt_service_address_);
    } catch (const std::runtime_error& e) {
        std::cerr << "[" << (user_data->sessionId.empty() ? "NO_SESSION_ID_YET" : user_data->sessionId)
                  << "] Failed to create STTClient: " << e.what() << ". Closing WebSocket." << std::endl;
        ws->end(1011, "Internal server error: STT client initialization failed");
        return;
    }
    user_data->stt_stream_active = false;

    {
        std::lock_guard<std::mutex> lock(active_websockets_mutex_);
        active_websockets_[user_data->sessionId] = ws;
    }

    std::cout << "[" << user_data->sessionId << "] WebSocket client connected from "
              << svToString(ws->getRemoteAddressAsText()) 
              << ". Total clients: " << connected_clients_count_.load() << std::endl;

    nlohmann::json session_info_payload = {
        {"type", "session_info"},
        {"sessionId", user_data->sessionId}
    };
    ws->send(session_info_payload.dump(), uWS::OpCode::TEXT);
    std::cout << "[" << user_data->sessionId << "] Sent 'session_info' to client." << std::endl;
}

void WebSocketServer::on_websocket_message(WebSocketConnection* ws, std::string_view message, uWS::OpCode op_code) {
    PerSocketData *user_data = ws->getUserData();
    if (!user_data || user_data->sessionId.empty()) { 
        std::cerr << "CRITICAL: PerSocketData is null or sessionId is empty in on_websocket_message. OpCode: "
                  << static_cast<int>(op_code) << ". RemoteIP: " << svToString(ws->getRemoteAddressAsText()) << std::endl;
        ws->end(1011, "Internal server error: session data missing or invalid");
        return;
    }
    const std::string& current_session_id = user_data->sessionId;

    if (op_code == uWS::OpCode::TEXT) {
        std::string message_str = svToString(message);
        try {
            nlohmann::json ctrl_msg = nlohmann::json::parse(message_str);

            if (ctrl_msg.contains("type")) {
                std::string type = ctrl_msg["type"];

                if (type == "start_stream") {
                    if (user_data->stt_stream_active && user_data->stt_client) {
                        std::cout << "[" << current_session_id << "] Received 'start_stream' while STT stream is already active. "
                                  << "Stopping previous STT stream and starting new." << std::endl;
                        user_data->stt_client->StopStreamNow(); 
                    }
                    
                    stt::RecognitionConfig stt_config;
                    stt_config.set_frontend_session_id(current_session_id); 
                    stt_config.set_session_id(current_session_id); 
                    stt_config.set_language(ctrl_msg.value("language", "ko-KR")); 

                    std::cout << "[" << current_session_id << "] Processing 'start_stream'. Lang: "
                              << stt_config.language() << ", FE_SID: " << stt_config.frontend_session_id() << std::endl;
                    
                    if (!user_data->stt_client) { 
                        std::cerr << "[" << current_session_id << "] ❌ STTClient is null before StartStream. Recreating." << std::endl;
                         try {
                            user_data->stt_client = std::make_unique<websocket_gateway::STTClient>(stt_service_address_);
                        } catch (const std::runtime_error& e) {
                             std::cerr << "[" << current_session_id << "] ❌ Failed to recreate STTClient in start_stream: " << e.what() << std::endl;
                             ws->send("{\"type\":\"error\", \"message\":\"STT client error on start_stream.\"}", uWS::OpCode::TEXT);
                             return;
                        }
                    }

                    bool started = user_data->stt_client->StartStream(stt_config,
                        [this, fe_sid = current_session_id, ws_captured = ws](const grpc::Status& status) { 
                            if (uWS::Loop::get()) { 
                                uWS::Loop::get()->defer([this, fe_sid, status, ws_captured]() {
                                    std::cout << "[" << fe_sid << "] STT gRPC stream Finish callback. Status: ("
                                              << status.error_code() << ") " << svToString(status.error_message()) << std::endl;
                                    
                                    WebSocketConnection* current_ws_deferred = find_websocket_by_session_id(fe_sid);
                                    if (current_ws_deferred && current_ws_deferred == ws_captured) {
                                        PerSocketData* current_data_deferred = current_ws_deferred->getUserData();
                                        if (current_data_deferred) {
                                           current_data_deferred->stt_stream_active = false; 
                                           std::cout << "[" << fe_sid << "] STT stream marked as inactive by gRPC callback." << std::endl;
                                        }
                                        nlohmann::json response_msg;
                                        if (!status.ok() && status.error_code() != grpc::StatusCode::CANCELLED) { 
                                            response_msg = {
                                                {"type", "error"}, {"source", "stt_service_grpc_finish"},
                                                {"code", status.error_code()}, {"message", svToString(status.error_message())}
                                            };
                                        } else if (status.ok()){ 
                                            response_msg = { {"type", "stt_stream_ended_by_server"}, {"sessionId", fe_sid} };
                                        }
                                        if (!response_msg.empty()) {
                                           current_ws_deferred->send(response_msg.dump(), uWS::OpCode::TEXT);
                                        }
                                    }
                                });
                            } else {
                                 std::cerr << "[" << fe_sid << "] uWS::Loop not available in STT Finish callback." << std::endl;
                            }
                        });

                    if (started) {
                        user_data->stt_stream_active = true; 
                        std::cout << "[" << current_session_id << "] STTClient->StartStream succeeded. STT stream active." << std::endl;
                        ws->send("{\"type\":\"stt_stream_started\"}", uWS::OpCode::TEXT);
                        std::cout << "[" << current_session_id << "] Sent 'stt_stream_started' to client." << std::endl;
                    } else {
                        std::cerr << "[" << current_session_id << "] ❌ FAILED to start STT stream with STTClient->StartStream." << std::endl;
                        ws->send("{\"type\":\"error\", \"message\":\"Failed to start STT stream with STT service (client init failed)\"}", uWS::OpCode::TEXT);
                        user_data->stt_stream_active = false; 
                    }
                } else if (type == "utterance_ended" || type == "stop_stream") {
                     std::cout << "[" << current_session_id << "] Processing '" << type << "' message." << std::endl;
                        if (user_data->stt_client && user_data->stt_stream_active) { 
                            std::cout << "[" << current_session_id << "] Calling STTClient->WritesDoneAndFinish() for '" << type << "'." << std::endl;
                            user_data->stt_client->WritesDoneAndFinish(); 
                            if (type == "stop_stream") { 
                                ws->send("{\"type\":\"stream_stopping_acknowledged\"}", uWS::OpCode::TEXT);
                            }
                        } else {
                            std::cout << "[" << current_session_id << "] STT stream not active or stt_client null. Ignoring '" << type << "'." << std::endl;
                            ws->send("{\"type\":\"info\", \"message\":\"STT stream not active for " + type + "\"}", uWS::OpCode::TEXT);
                        }
                } else if (type == "heartbeat") {
                    ws->send("{\"type\":\"heartbeat_ack\"}", uWS::OpCode::TEXT);
                } else {
                    std::cerr << "[" << current_session_id << "] Unknown TEXT message type: " << type << std::endl;
                    ws->send("{\"type\":\"error\", \"message\":\"Unknown message type: " + type + "\"}", uWS::OpCode::TEXT);
                }
            } else {
                 std::cerr << "[" << current_session_id << "] Received TEXT message without 'type' field: " << message_str << std::endl;
                 ws->send("{\"type\":\"error\", \"message\":\"Message format error: 'type' field missing\"}", uWS::OpCode::TEXT);
            }
        } catch (const nlohmann::json::parse_error& e) {
            std::cerr << "[" << current_session_id << "] JSON parse error: " << e.what() << " on message: " << message_str << std::endl;
            ws->send("{\"type\":\"error\", \"message\":\"Invalid JSON format\"}", uWS::OpCode::TEXT);
        } catch (const std::exception& e) {
            std::cerr << "[" << current_session_id << "] Exception processing TEXT message: " << e.what() << std::endl;
            ws->send("{\"type\":\"error\", \"message\":\"Server error processing message\"}", uWS::OpCode::TEXT);
        }

    } else if (op_code == uWS::OpCode::BINARY) {
        if (user_data->stt_client && user_data->stt_stream_active) { 
            total_audio_bytes_processed_stt_ += message.length();
            if (!user_data->stt_client->WriteAudioChunk(std::string(message))) { 
                 std::cerr << "[" << current_session_id << "] ❌ FAILED to write audio chunk to STTClient. Marking STT stream as inactive and stopping." << std::endl;
                 user_data->stt_stream_active = false; 
                 user_data->stt_client->StopStreamNow(); 
                 
                 nlohmann::json err_msg = {{"type", "error"}, {"source", "audio_chunk_send"}, {"message", "Failed to send audio to STT service. Please restart."}};
                 ws->send(err_msg.dump(), uWS::OpCode::TEXT);
            }
        }
    } else { 
        std::cout << "[" << current_session_id << "] Received message with unhandled OpCode: " << static_cast<int>(op_code) << std::endl;
    }
}

void WebSocketServer::on_websocket_close(WebSocketConnection* ws, int code, std::string_view message) {
    connected_clients_count_--; 
    PerSocketData *user_data = ws->getUserData();
    if (!user_data) { 
        std::cerr << "WebSocket client disconnected but PerSocketData was null. Code: " << code 
                  << ", Msg: \"" << svToString(message) << "\""
                  << ", RemoteIP: " << svToString(ws->getRemoteAddressAsText()) 
                  << std::endl;
        return;
    }
    std::string session_id_copy = user_data->sessionId; 

    std::cout << "[" << session_id_copy << "] WebSocket client disconnected. Code: " << code 
              << ", Msg: \"" << svToString(message) << "\""
              << ", RemoteIP: " << svToString(ws->getRemoteAddressAsText())
              << ". Total clients: " << connected_clients_count_.load() << std::endl;

    if (user_data->stt_client) { 
        if (user_data->stt_stream_active) { 
            std::cout << "[" << session_id_copy << "] Forcing STT stream stop (StopStreamNow) due to WebSocket close." << std::endl;
            user_data->stt_client->StopStreamNow(); 
        }
    }

    {
        std::lock_guard<std::mutex> lock(active_websockets_mutex_);
        active_websockets_.erase(session_id_copy); 
    }
}

void WebSocketServer::handle_health_check(uWS::HttpResponse<GLOBAL_SSL_ENABLED>* res, uWS::HttpRequest* req) {
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


} // namespace websocket_gateway
