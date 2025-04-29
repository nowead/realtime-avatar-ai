#include "avatar.pb.h"
#include "websocket_dispatcher_uwebs.h"
#include <nlohmann/json.hpp>
#include <iostream>

using json = nlohmann::json;

void WebSocketDispatcherUWS::registerSession(const std::string& session_id, std::shared_ptr<void> conn) {
    auto ws = static_cast<WS*>(conn.get());
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[session_id] = ws;
    std::cout << "✅ WebSocket session registered: " << session_id << std::endl;
}

void WebSocketDispatcherUWS::unregisterSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(session_id);
    std::cout << "❌ WebSocket session removed: " << session_id << std::endl;
}

bool WebSocketDispatcherUWS::sendToClient(const std::string& session_id,
                                          const std::vector<uint8_t>& audio_data,
                                          const std::vector<avatar::Viseme>& visemes,
                                          const std::string& format) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        std::cerr << "⚠️ Session not found: " << session_id << std::endl;
        return false;
    }

    json message;
    message["type"] = "avatar_sync";
    message["format"] = format;
    message["visemes"] = json::array();

    for (const auto& v : visemes) {
        message["visemes"].push_back({
            {"timestamp_ms", v.timestamp_ms()},
            {"viseme", v.viseme_id()}
        });
    }

    // 전송 (audio_data는 따로 바이너리 전송 또는 base64 가능)
    std::string payload = message.dump();
    it->second->send(payload, uWS::OpCode::TEXT);

    return true;
}
