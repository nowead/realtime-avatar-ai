#pragma once

#include "websocket_dispatcher.h"
#include <uwebsockets/App.h>
#include <unordered_map>
#include <mutex>

class WebSocketDispatcherUWS : public WebSocketDispatcher {
public:
    using WS = uWS::WebSocket<false, true, struct PerSocketData>;

    void registerSession(const std::string& session_id, std::shared_ptr<void> connection) override;
    void unregisterSession(const std::string& session_id) override;

    bool sendToClient(const std::string& session_id,
                      const std::vector<uint8_t>& audio_data,
                      const std::vector<avatar::Viseme>& visemes,
                      const std::string& format) override;

private:
    std::unordered_map<std::string, WS*> sessions_;
    std::mutex mutex_;
};
