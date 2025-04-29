#include "websocket_client.h"
#include <iostream>
#include <chrono>

WebSocketClient::WebSocketClient() {
    // 이벤트 루프 스레드 시작
    event_thread_ = std::thread(&WebSocketClient::runEventLoop, this);
}

WebSocketClient::~WebSocketClient() {
    should_run_ = false;
    disconnect(); // 연결 종료 시도
    if(loop_) {
        loop_->defer([this](){ /* 루프 종료 전 정리 작업 */ }); // 루프 종료 전 작업 예약 (선택적)
        // uWS 루프 종료 방법 확인 필요 (공식 문서 참조)
    }
    if (event_thread_.joinable()) {
        event_thread_.join();
    }
}

void WebSocketClient::runEventLoop() {
    loop_ = uWS::Loop::get(); // 스레드 로컬 루프 가져오기

    while(should_run_) {
        if (!target_uri_.empty() && !isConnected()) {
             // 루프 내에서 연결 시도
             std::cout << "[WS Client Thread] Attempting to connect to " << target_uri_ << "..." << std::endl;
             uWS::App temp_app; // 연결 시도만을 위한 임시 App 객체
             temp_app.ws<void>("/*", {
                 .upgrade = nullptr,
                 .open = [this](auto *ws) {
                     std::lock_guard<std::mutex> lock(ws_mutex_);
                     ws_ = ws; // 연결 성공 시 저장
                     is_connected_ = true;
                     std::cout << "🚀 [WS Client Thread] Connected!" << std::endl;
                     if (connect_handler_) {
                         loop_->defer([this](){ connect_handler_(); }); // 메인 루프에서 핸들러 호출
                     }
                 },
                 .message = [this](auto *ws, std::string_view message, uWS::OpCode opCode) {
                     if (message_handler_) {
                          // 핸들러를 직접 호출하거나, 메인 스레드로 전달하는 큐 사용 가능
                         message_handler_(message, opCode);
                     }
                 },
                 .close = [this](auto *ws, int code, std::string_view message) {
                     std::lock_guard<std::mutex> lock(ws_mutex_);
                     std::cout << "🔌 [WS Client Thread] Disconnected. Code: " << code << std::endl;
                     is_connected_ = false;
                     ws_ = nullptr; // 연결 끊김 표시
                     if (disconnect_handler_) {
                         loop_->defer([this, code, msg = std::string(message)](){ disconnect_handler_(code, msg); });
                     }
                     target_uri_.clear(); // 재연결 방지 (connect 함수에서 다시 설정)
                 }
             }).connect(target_uri_, nullptr); // 실제 연결 시도
             target_uri_.clear(); // 연결 시도 후 클리어 (성공/실패 관계 없이)
         }

        // 이벤트 처리 (논블로킹 또는 블로킹 방식 선택)
        loop_->runOnce(); // 논블로킹 방식: 다른 작업과 병행 가능
        // loop_->run(); // 블로킹 방식: 이 스레드는 여기서 대기

        // CPU 사용률 제어를 위한 짧은 대기
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::cout << "[WS Client Thread] Event loop finished." << std::endl;
}


void WebSocketClient::connect(const std::string& uri) {
    if (isConnected() || !target_uri_.empty()) { // 이미 연결 중이거나 연결 시도 중이면 반환
        std::cerr << "⚠️ Already connected or connection attempt in progress." << std::endl;
        return;
    }
     target_uri_ = uri; // 연결할 주소 저장 (이벤트 루프 스레드에서 사용)
     // 실제 연결은 이벤트 루프 스레드에서 수행됨
}

void WebSocketClient::disconnect() {
    if (isConnected() && loop_) {
        loop_->defer([this]() { // 루프 스레드에서 안전하게 종료
            std::lock_guard<std::mutex> lock(ws_mutex_);
            if (ws_) {
                ws_->close();
                ws_ = nullptr; // 포인터 즉시 무효화
            }
            is_connected_ = false;
        });
    }
    target_uri_.clear(); // 연결 시도 중지
}

bool WebSocketClient::sendBinary(const std::vector<uint8_t>& data) {
    if (!isConnected() || !loop_) return false;

    // 데이터를 복사하여 루프 스레드로 전달 (수명 문제 방지)
    auto data_copy = std::make_shared<std::vector<uint8_t>>(data);

    loop_->defer([this, data_copy]() {
         std::lock_guard<std::mutex> lock(ws_mutex_);
         if (ws_) {
             ws_->send(std::string_view(reinterpret_cast<char*>(data_copy->data()), data_copy->size()), uWS::OpCode::BINARY);
         }
    });
    return true;
}

bool WebSocketClient::sendText(const std::string& message) {
     if (!isConnected() || !loop_) return false;

     auto msg_copy = std::make_shared<std::string>(message);

     loop_->defer([this, msg_copy]() {
         std::lock_guard<std::mutex> lock(ws_mutex_);
         if (ws_) {
             ws_->send(*msg_copy, uWS::OpCode::TEXT);
         }
     });
     return true;
}


void WebSocketClient::onMessage(MessageHandler handler) {
    message_handler_ = std::move(handler);
}

void WebSocketClient::onConnect(ConnectHandler handler) {
    connect_handler_ = std::move(handler);
}

void WebSocketClient::onDisconnect(DisconnectHandler handler) {
    disconnect_handler_ = std::move(handler);
}

bool WebSocketClient::isConnected() const {
    return is_connected_;
}