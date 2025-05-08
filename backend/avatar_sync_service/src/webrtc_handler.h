#ifndef WEBRTC_HANDLER_H
#define WEBRTC_HANDLER_H

#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <unordered_map>

// WebRTC 관련 헤더 (사용하는 라이브러리에 따라 다름)
// 예시: #include "api/peer_connection_interface.h"
// 예시: #include "api/data_channel_interface.h"

// 이 예제에서는 WebRTC 데이터 채널을 추상화한 간단한 인터페이스를 사용합니다.
// 실제로는 WebRTC 라이브러리의 데이터 채널 객체를 사용해야 합니다.
class AbstractDataChannel {
public:
    virtual ~AbstractDataChannel() = default;
    virtual void SendAudio(const std::vector<uint8_t>& audio_data) = 0;
    virtual void SendViseme(const std::string& viseme_json_data) = 0; // VisemeData를 JSON 문자열 등으로 변환하여 전송
};

class WebRTCHandler {
public:
    WebRTCHandler();
    ~WebRTCHandler();

    // 세션 ID를 기반으로 WebRTC 연결(및 데이터 채널)을 준비/가져옵니다.
    // 실제 구현에서는 PeerConnection 생성, 시그널링, 데이터 채널 생성 등이 포함됩니다.
    std::shared_ptr<AbstractDataChannel> GetDataChannelForSession(const std::string& session_id);

    // 이 메서드들은 실제로는 AbstractDataChannel 구현체 내부로 이동할 수 있습니다.
    void SendAudioData(const std::string& session_id, const std::vector<uint8_t>& audio_chunk);
    void SendVisemeData(const std::string& session_id, const std::string& viseme_id, double start_time_sec, float duration_sec);

    // WebRTC 연결 종료 등 정리 작업
    void CleanupSession(const std::string& session_id);

private:
    // 세션 ID별 데이터 채널 관리 (실제로는 PeerConnection 객체 등을 관리)
    std::unordered_map<std::string, std::shared_ptr<AbstractDataChannel>> session_data_channels_;
    std::mutex mutex_;

    // 임시 DataChannel 구현 (실제 WebRTC 라이브러리로 대체 필요)
    class DummyDataChannel : public AbstractDataChannel {
    public:
        DummyDataChannel(const std::string& session_id);
        void SendAudio(const std::vector<uint8_t>& audio_data) override;
        void SendViseme(const std::string& viseme_json_data) override;
    private:
        std::string session_id_;
    };
};

#endif // WEBRTC_HANDLER_H