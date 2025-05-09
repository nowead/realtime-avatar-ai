// src/signaling/SignalingProtocol.h
#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace signaling::protocol {

enum class MessageType { Offer, Answer, IceCandidate, Unknown };

struct SignalingMessage {
    MessageType type;
    std::string session_id;
    std::string sdp;
    std::string candidate;
    std::string sdp_mid;
    int sdp_mline_index;
};

inline nlohmann::json to_json(const SignalingMessage& m) {
    nlohmann::json j;
    switch (m.type) {
        case MessageType::Offer:        j["type"] = "offer"; break;
        case MessageType::Answer:       j["type"] = "answer"; break;
        case MessageType::IceCandidate: j["type"] = "ice-candidate"; break;
        default:                        j["type"] = "unknown"; break;
    }
    j["session_id"] = m.session_id;
    if (m.type == MessageType::Offer || m.type == MessageType::Answer)
        j["sdp"] = m.sdp;
    if (m.type == MessageType::IceCandidate) {
        j["candidate"]      = m.candidate;
        j["sdpMid"]         = m.sdp_mid;
        j["sdpMLineIndex"]  = m.sdp_mline_index;
    }
    return j;
}

inline std::string serialize(const SignalingMessage& m) {
    return to_json(m).dump();
}

inline SignalingMessage parse(const std::string& text) {
    auto j = nlohmann::json::parse(text);
    SignalingMessage m{};
    std::string t = j.value("type","");
    if      (t=="offer")        m.type = MessageType::Offer;
    else if (t=="answer")       m.type = MessageType::Answer;
    else if (t=="ice-candidate")m.type = MessageType::IceCandidate;
    else                         m.type = MessageType::Unknown;
    m.session_id      = j.value("session_id","");
    if (m.type==MessageType::Offer||m.type==MessageType::Answer)
        m.sdp = j.value("sdp","");
    if (m.type==MessageType::IceCandidate) {
        m.candidate      = j.value("candidate","");
        m.sdp_mid        = j.value("sdpMid","");
        m.sdp_mline_index= j.value("sdpMLineIndex",0);
    }
    return m;
}

} // namespace signaling::protocol
