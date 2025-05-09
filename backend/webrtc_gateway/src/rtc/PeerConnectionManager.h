// src/rtc/PeerConnectionManager.h
#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <api/peer_connection_interface.h>
#include <api/create_peerconnection_factory.h>
#include <rtc_base/thread.h>
#include <nlohmann/json.hpp>
#include "../grpc_clients/SttClient.h"
#include "../grpc_clients/TtsClient.h"

namespace rtc {

using SignalCallback = std::function<void(const std::string&)>;

class PeerConnectionManager
    : public webrtc::PeerConnectionObserver,
      public webrtc::CreateSessionDescriptionObserver,
      public webrtc::SetSessionDescriptionObserver,
      public webrtc::DataChannelObserver {
public:
    explicit PeerConnectionManager(std::shared_ptr<SttClient> stt,
                                   std::shared_ptr<TtsClient> tts);
    ~PeerConnectionManager() override;

    bool createPeerConnection(const std::string& session_id);
    bool handleRemoteDescription(const std::string& session_id,
                                 const std::string& sdp,
                                 webrtc::SdpType type);
    bool addIceCandidate(const std::string& session_id,
                         const std::string& candidate,
                         const std::string& sdp_mid,
                         int sdp_mline_index);
    void registerSignalingChannel(const std::string& session_id,
                                  SignalCallback cb);

    void startTtsStream(const std::string& session_id);
    void sendAudioFrame(const std::string& session_id,
                        const void* data,
                        int bits_per_sample,
                        int sample_rate,
                        size_t channels,
                        size_t frames);
    void sendViseme(const std::string& session_id,
                    const nlohmann::json& viseme_json);

    rtc::scoped_refptr<webrtc::PeerConnectionInterface>
    getPeerConnection(const std::string& session_id) const;

    // PeerConnectionObserver
    void OnIceCandidate(const webrtc::IceCandidateInterface* ice) override;
    void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> channel) override {}
    void OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override {}
    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) override {}
    void OnStandardizedIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState) override {}
    void OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState) override {}
    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState) override {}
    void OnRemoveTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface>) override {}
    void OnRenegotiationNeeded() override {}

    // CreateSessionDescriptionObserver
    void OnSuccess(webrtc::SessionDescriptionInterface* desc) override;
    void OnFailure(webrtc::RTCError error) override;

    // SetSessionDescriptionObserver
    void OnSetSuccess() override {}
    void OnSetFailure(webrtc::RTCError error) override;

private:
    std::shared_ptr<SttClient> stt_client_;
    std::shared_ptr<TtsClient> tts_client_;
    std::unordered_map<std::string,bool> stt_client_started_;

    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory_;
    std::unique_ptr<rtc::Thread> net_thread_, worker_thread_, signal_thread_;

    struct SessionInfo {
        rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc;
        rtc::scoped_refptr<webrtc::DataChannelInterface>   dc;
        rtc::scoped_refptr<cricket::FakeAudioTrackSource>  src;
        rtc::scoped_refptr<webrtc::AudioTrackInterface>   track;
        SignalCallback                                    signal_cb;
    };

    std::unordered_map<std::string, SessionInfo> sessions_;
    mutable std::mutex mu_;
};

} // namespace rtc
