// src/signaling/WebSocketServer.h
#pragma once

#include <boost/asio.hpp>
#include <boost/beast/websocket.hpp>
#include <memory>
#include "../rtc/PeerConnectionManager.h"

namespace signaling {

using tcp = boost::asio::ip::tcp;
namespace websocket = boost::beast::websocket;

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
public:
    WebSocketSession(tcp::socket socket,
                     std::shared_ptr<rtc::PeerConnectionManager> pcmgr);
    void start();

private:
    websocket::stream<tcp::socket> ws_;
    boost::beast::flat_buffer       buffer_;
    std::shared_ptr<rtc::PeerConnectionManager> pcmgr_;
    std::string sdp_answer_;

    void onAccept(boost::system::error_code ec);
    void doRead();
    void onRead(boost::system::error_code ec, std::size_t bytes_transferred);
    void sendMessage(const std::string& msg);
    void onWrite(boost::system::error_code ec, std::size_t bytes_transferred);
};

class WebSocketServer {
public:
    WebSocketServer(boost::asio::io_context& ioc,
                    tcp::endpoint endpoint,
                    std::shared_ptr<rtc::PeerConnectionManager> pcmgr);
    void run();

private:
    boost::asio::io_context& ioc_;
    tcp::acceptor            acceptor_;
    std::shared_ptr<rtc::PeerConnectionManager> pcmgr_;
};

} // namespace signaling
