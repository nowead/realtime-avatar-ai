#include "webrtc_handler.h"
#include <iostream> // 로깅 및 디버깅용

// 실제 WebRTC 라이브러리 사용 시 필요한 헤더 및 네임스페이스
// #include "rtc_base/logging.h"

WebRTCHandler::WebRTCHandler() {
    // WebRTC 초기화 코드 (예: 스레드 풀, 팩토리 객체 생성 등)
    std::cout << "WebRTCHandler initialized." << std::endl;
}

WebRTCHandler::~WebRTCHandler() {
    // WebRTC 정리 코드
    std::cout << "WebRTCHandler destroyed." << std::endl;
    session_data_channels_.clear();
}

// DummyDataChannel 구현
WebRTCHandler::DummyDataChannel::DummyDataChannel(const std::string& sid) : session_id_(sid) {
    std::cout << "DummyDataChannel created for session: " << session_id_ << std::endl;
}

void WebRTCHandler::DummyDataChannel::SendAudio(const std::vector<uint8_t>& audio_data) {
    // 실제로는 WebRTC 데이터 채널을 통해 바이너리 데이터 전송
    std::cout << "Session [" << session_id_ << "] Sending audio data (size: " << audio_data.size() << " bytes) via WebRTC DataChannel." << std::endl;
    // 예: data_channel_->Send(webrtc::DataBuffer(rtc::CopyOnWriteBuffer(audio_data.data(), audio_data.size()), true));
}

void WebRTCHandler::DummyDataChannel::SendViseme(const std::string& viseme_json_data) {
    // 실제로는 WebRTC 데이터 채널을 통해 텍스트 (JSON) 데이터 전송
    std::cout << "Session [" << session_id_ << "] Sending viseme data: " << viseme_json_data << " via WebRTC DataChannel." << std::endl;
    // 예: data_channel_->Send(webrtc::DataBuffer(viseme_json_data));
}


std::shared_ptr<AbstractDataChannel> WebRTCHandler::GetDataChannelForSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (session_data_channels_.find(session_id) == session_data_channels_.end()) {
        std::cout << "Creating new WebRTC data channel for session: " << session_id << std::endl;
        // 여기에 실제 WebRTC PeerConnection 및 DataChannel 생성/설정 로직이 들어갑니다.
        // 시그널링 서버와의 통신을 통해 ICE 후보 교환, SDP 제안/응답 처리 등이 필요합니다.
        // 지금은 DummyDataChannel을 사용합니다.
        session_data_channels_[session_id] = std::make_shared<DummyDataChannel>(session_id);
    }
    return session_data_channels_[session_id];
}

void WebRTCHandler::SendAudioData(const std::string& session_id, const std::vector<uint8_t>& audio_chunk) {
    auto data_channel = GetDataChannelForSession(session_id);
    if (data_channel) {
        data_channel->SendAudio(audio_chunk);
    } else {
        std::cerr << "Error: No data channel for session " << session_id << std::endl;
    }
}

void WebRTCHandler::SendVisemeData(const std::string& session_id, const std::string& viseme_id, double start_time_sec, float duration_sec) {
    auto data_channel = GetDataChannelForSession(session_id);
    if (data_channel) {
        // VisemeData를 JSON 문자열 등으로 변환하여 전송
        // 간단한 예시: {"viseme_id": "AA", "start_time": 1.23, "duration": 0.1}
        // 실제로는 JSON 라이브러리(nlohmann/json 등)를 사용하는 것이 좋습니다.
        std::string viseme_json = "{";
        viseme_json += "\"viseme_id\":\"" + viseme_id + "\",";
        viseme_json += "\"start_time_sec\":" + std::to_string(start_time_sec) + ",";
        viseme_json += "\"duration_sec\":" + std::to_string(duration_sec);
        viseme_json += "}";
        data_channel->SendViseme(viseme_json);
    } else {
        std::cerr << "Error: No data channel for session " << session_id << std::endl;
    }
}

void WebRTCHandler::CleanupSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = session_data_channels_.find(session_id);
    if (it != session_data_channels_.end()) {
        std::cout << "Cleaning up WebRTC session: " << session_id << std::endl;
        // 실제 WebRTC PeerConnection 종료 로직
        // it->second->Close(); // 예시
        session_data_channels_.erase(it);
    }
}