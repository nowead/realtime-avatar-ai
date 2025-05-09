#include "WebSocketServer.h"
#include <iostream>

namespace signaling {

WebSocketServer::WebSocketServer(boost::asio::io_context& ioc,
                                 tcp::endpoint endpoint,
                                 std::shared_ptr<rtc::PeerConnectionManager> pcmgr)
    : ioc_(ioc), acceptor_(ioc), pcmgr_(std::move(pcmgr)) {
    boost::system::error_code ec;
    acceptor_.open(endpoint.protocol(), ec);
    acceptor_.set_option(boost::asio::socket_base::reuse_address(true), ec);
    acceptor_.bind(endpoint, ec);
    acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
}

void WebSocketServer::run() {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::make_shared<WebSocketSession>(
                    std::move(socket), pcmgr_)->start();
            } else {
                std::cerr << "Accept error: " << ec.message() << std::endl;
            }
            run();
        }
    );
}

} // namespace signaling