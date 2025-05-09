// src/signaling/WebSocketSession.cpp
#include "WebSocketServer.h"
#include "SignalingProtocol.h"
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <iostream>

namespace signaling {

using protocol::MessageType;
using protocol::SignalingMessage;
using protocol::parse;
using protocol::serialize;

WebSocketSession::WebSocketSession(tcp::socket socket,
                                   std::shared_ptr<rtc::PeerConnectionManager> pcmgr)
    : ws_(std::move(socket)), pcmgr_(std::move(pcmgr)) {}

void WebSocketSession::start() {
    ws_.async_accept(
        std::bind(&WebSocketSession::onAccept,
                  shared_from_this(),
                  std::placeholders::_1));
}

void WebSocketSession::onAccept(boost::system::error_code ec) {
    if (ec) {
        std::cerr << "WS accept error: " << ec.message() << std::endl;
        return;
    }
    doRead();
}

void WebSocketSession::doRead() {
    ws_.async_read(
        buffer_,
        std::bind(&WebSocketSession::onRead,
                  shared_from_this(),
                  std::placeholders::_1,
                  std::placeholders::_2));
}

void WebSocketSession::onRead(boost::system::error_code ec,
                              std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);
    if (ec == websocket::error::closed) return;
    if (ec) {
        std::cerr << "WS read error: " << ec.message() << std::endl;
        return;
    }

    std::string raw = boost::beast::buffers_to_string(buffer_.data());
    buffer_.consume(buffer_.size());
    SignalingMessage msg = parse(raw);

    switch (msg.type) {
    case MessageType::Offer: {
        // 시그널링 채널 등록
        pcmgr_->registerSignalingChannel(
            msg.session_id,
            [self = shared_from_this()](const std::string& m) {
                self->sendMessage(m);
            }
        );
        // PeerConnection 생성 및 Remote SDP 설정
        pcmgr_->createPeerConnection(msg.session_id);
        pcmgr_->handleRemoteDescription(
            msg.session_id, msg.sdp, webrtc::SdpType::kOffer
        );
        // Answer 생성 (콜백은 PeerConnectionManager::OnSuccess)
        {
            auto pc = pcmgr_->getPeerConnection(msg.session_id);
            if (pc) {
                pc->CreateAnswer(
                    pcmgr_.get(),  // this를 CreateSessionDescriptionObserver로
                    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions()
                );
            }
        }
        // TTS 스트림 시작
        pcmgr_->startTtsStream(msg.session_id);
        break;
    }
    case MessageType::IceCandidate:
        pcmgr_->addIceCandidate(
            msg.session_id,
            msg.candidate,
            msg.sdp_mid,
            msg.sdp_mline_index
        );
        break;
    default:
        std::cerr << "Unknown signaling type" << std::endl;
    }

    // 에코(테스트용)
    sendMessage(raw);
}

void WebSocketSession::sendMessage(const std::string& msg) {
    ws_.async_write(
        boost::asio::buffer(msg),
        std::bind(&WebSocketSession::onWrite,
                  shared_from_this(),
                  std::placeholders::_1,
                  std::placeholders::_2));
}

void WebSocketSession::onWrite(boost::system::error_code ec,
                               std::size_t /*bytes*/) {
    if (ec) {
        std::cerr << "WS write error: " << ec.message() << std::endl;
        return;
    }
    doRead();
}

} // namespace signaling
