#pragma once

#include "avatar.pb.h"
#include <string>
#include <vector>
#include <memory>

class WebSocketDispatcher {
public:
    virtual ~WebSocketDispatcher() = default;

    virtual void registerSession(const std::string& session_id, std::shared_ptr<void> connection) = 0;
    virtual void unregisterSession(const std::string& session_id) = 0;

    virtual bool sendToClient(const std::string& session_id,
                               const std::vector<uint8_t>& audio_data,
                               const std::vector<avatar::Viseme>& visemes,
                               const std::string& format) = 0;
};
