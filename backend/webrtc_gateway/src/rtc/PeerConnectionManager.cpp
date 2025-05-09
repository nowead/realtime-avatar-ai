// src/rtc/PeerConnectionManager.cpp
#include "PeerConnectionManager.h"
#include "SignalingProtocol.h"
#include <api/mediastreaminterface.h>
#include <cricket/media/base/fake_audio_track_source.h>
#include <iostream>

using signaling::protocol::SignalingMessage;
using signaling::protocol::MessageType;
using signaling::protocol::serialize;

namespace rtc {

PeerConnectionManager::PeerConnectionManager(std::shared_ptr<SttClient> stt,
                                             std::shared_ptr<TtsClient> tts)
  : stt_client_(std::move(stt)),
    tts_client_(std::move(tts)) {
    net_thread_    = rtc::Thread::CreateWithSocketServer(); net_thread_->Start();
    worker_thread_ = rtc::Thread::Create();               worker_thread_->Start();
    signal_thread_ = rtc::Thread::Create();               signal_thread_->Start();

    factory_ = webrtc::CreatePeerConnectionFactory(
        net_thread_.get(), worker_thread_.get(), signal_thread_.get(),
        nullptr,
        webrtc::CreateAudioEncoderFactory(),
        webrtc::CreateAudioDecoderFactory(),
        nullptr, nullptr);
    if (!factory_) throw std::runtime_error("Failed to create PeerConnectionFactory");
}

PeerConnectionManager::~PeerConnectionManager() {
    for (auto& kv : stt_client_started_) {
        if (kv.second) stt_client_->FinishStream(kv.first);
    }
    tts_client_->Stop();

    sessions_.clear();
    signal_thread_->Stop();
    worker_thread_->Stop();
    net_thread_->Stop();
}

bool PeerConnectionManager::createPeerConnection(const std::string& id) {
    std::lock_guard<std::mutex> lk(mu_);
    if (sessions_.count(id)) return false;

    SessionInfo s;
    s.pc = factory_->CreatePeerConnection(
        webrtc::PeerConnectionInterface::RTCConfiguration(),
        nullptr, nullptr, this);
    if (!s.pc) return false;

    s.dc = s.pc->CreateDataChannel("viseme", nullptr);
    s.dc->RegisterObserver(this);

    auto *fake_src = new cricket::FakeAudioTrackSource(false);
    s.src   = fake_src;
    s.track = factory_->CreateAudioTrack("audio", fake_src);
    s.pc->AddTrack(s.track, {"stream"});

    sessions_[id] = std::move(s);
    stt_client_started_[id] = false;
    return true;
}

void PeerConnectionManager::registerSignalingChannel(const std::string& id,
                                                     SignalCallback cb) {
    std::lock_guard<std::mutex> lk(mu_);
    sessions_[id].signal_cb = std::move(cb);
}

bool PeerConnectionManager::handleRemoteDescription(const std::string& id,
                                                    const std::string& sdp,
                                                    webrtc::SdpType type) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return false;

    webrtc::SdpParseError err;
    auto desc = webrtc::CreateSessionDescription(type, sdp, &err);
    if (!desc) {
        std::cerr << "SDP parse error: " << err.description << "\n";
        return false;
    }
    it->second.pc->SetRemoteDescription(this, desc.release());
    return true;
}

bool PeerConnectionManager::addIceCandidate(const std::string& id,
                                            const std::string& candidate,
                                            const std::string& sdp_mid,
                                            int sdp_mline_index) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return false;

    webrtc::SdpParseError err;
    auto ice = webrtc::CreateIceCandidate(sdp_mid, sdp_mline_index, candidate, &err);
    if (!ice) {
        std::cerr << "ICE parse error: " << err.description << "\n";
        return false;
    }
    return it->second.pc->AddIceCandidate(ice.get());
}

void PeerConnectionManager::OnIceCandidate(const webrtc::IceCandidateInterface* ice) {
    SignalingMessage msg;
    msg.type            = MessageType::IceCandidate;
    ice->ToString(&msg.candidate);
    msg.sdp_mid         = ice->sdp_mid();
    msg.sdp_mline_index = ice->sdp_mline_index();

    std::lock_guard<std::mutex> lk(mu_);
    for (auto& [sid, info] : sessions_) {
        if (info.signal_cb) {
            msg.session_id = sid;
            info.signal_cb(serialize(msg));
        }
    }
}

void PeerConnectionManager::OnSuccess(webrtc::SessionDescriptionInterface* desc) {
    std::string sdp;
    desc->ToString(&sdp);

    SignalingMessage msg;
    msg.type       = MessageType::Answer;
    msg.sdp        = sdp;

    std::lock_guard<std::mutex> lk(mu_);
    for (auto& [sid, info] : sessions_) {
        if (info.signal_cb) {
            msg.session_id = sid;
            info.signal_cb(serialize(msg));
        }
    }
}

void PeerConnectionManager::OnFailure(webrtc::RTCError error) {
    std::cerr << "SDP error: " << error.message() << "\n";
}

void PeerConnectionManager::sendAudioFrame(const std::string& id,
                                           const void* data,
                                           int bps,
                                           int sr,
                                           size_t ch,
                                           size_t fn) {
    std::lock_guard<std::mutex> lk(mu_);
    auto& info = sessions_[id];
    auto* fs    = static_cast<cricket::FakeAudioTrackSource*>(info.src.get());
    cricket::AudioFrame frame;
    frame.timestamp_           = 0;
    frame.sample_rate_hz_      = sr;
    frame.num_channels_        = ch;
    frame.samples_per_channel_ = fn;
    frame.speech_              = true;
    memcpy(frame.mutable_data(), data, fn * ch * (bps/8));
    fs->PushFrame(frame);
}

void PeerConnectionManager::startTtsStream(const std::string& id) {
    tts_client_->Start(
        id,
        [this,id](const void* d,int bps,int sr,size_t ch,size_t fn){
            sendAudioFrame(id,d,bps,sr,ch,fn);
        },
        [this,id](const avatar_sync::VisemeData& v){
            nlohmann::json j{
                {"viseme_id",    v.viseme_id()},
                {"start_time",   v.start_time().seconds()},
                {"duration_sec", v.duration_sec()}
            };
            sendViseme(id,j);
        }
    );
}

void PeerConnectionManager::sendViseme(const std::string& id,
                                       const nlohmann::json& j) {
    SignalingMessage msg;
    msg.type       = MessageType::Answer;  // 필요 시 Viseme 전용 타입 정의
    msg.session_id = id;
    msg.sdp        = j.dump();

    std::lock_guard<std::mutex> lk(mu_);
    auto& cb = sessions_[id].signal_cb;
    if (cb) cb(serialize(msg));
}

rtc::scoped_refptr<webrtc::PeerConnectionInterface>
PeerConnectionManager::getPeerConnection(const std::string& id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = sessions_.find(id);
    return (it != sessions_.end()) ? it->second.pc : nullptr;
}

} // namespace rtc
