#pragma once

#include <uwebsockets/App.h> // 편의상 여기에 포함 (또는 Pimpl 사용)
#include <string>
#include <functional>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>

class WebSocketClient {
public:
    using MessageHandler = std::function<void(std::string_view, uWS::OpCode)>;
    using ConnectHandler = std::function<void()>;
    using DisconnectHandler = std::function<void(int, std::string_view)>;

    WebSocketClient();
    ~WebSocketClient();

    // 서버 연결 시도 (비동기)
    void connect(const std::string& uri);

    // 연결 종료
    void disconnect();

    // 데이터 전송
    bool sendBinary(const std::vector<uint8_t>& data);
    bool sendText(const std::string& message);

    // 이벤트 핸들러 등록
    void onMessage(MessageHandler handler);
    void onConnect(ConnectHandler handler);
    void onDisconnect(DisconnectHandler handler);

    bool isConnected() const;

private:
    void runEventLoop();

    uWS::Loop* loop_ = nullptr; // 이벤트 루프 포인터
    uWS::WebSocket<false, true, void>* ws_ = nullptr; // 클라이언트 소켓 포인터
    std::thread event_thread_;
    std::atomic<bool> is_connected_ = false;
    std::atomic<bool> should_run_ = true;
    std::string target_uri_;
    std::mutex ws_mutex_; // ws_ 포인터 접근 보호

    // 핸들러 저장
    MessageHandler message_handler_;
    ConnectHandler connect_handler_;
    DisconnectHandler disconnect_handler_;
};