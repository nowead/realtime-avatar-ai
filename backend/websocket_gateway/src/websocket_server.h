#ifndef WEBSOCKET_SERVER_H
#define WEBSOCKET_SERVER_H

#include <App.h> // uWebSockets 기본 헤더
#include <string>
#include <functional>
#include <map>
#include <mutex>
#include <atomic>
#include <memory>
#include "types.h"      // PerSocketData 정의 (이 안에는 stt_client.h가 포함되어야 함)
                        // types.h 내의 PerSocketData::stt_client는 
                        // std::unique_ptr<websocket_gateway::STTClient> 여야 합니다.

// uWebSockets 관련 전역 상수 정의
constexpr bool GLOBAL_SSL_ENABLED = false;
// uWS::DISABLED, uWS::SHARED_COMPRESSOR, uWS::DEDICATED_COMPRESSOR (뒤에 크기 지정 가능)
constexpr uWS::CompressOptions GLOBAL_COMPRESSION_OPTIONS = uWS::SHARED_COMPRESSOR; 
constexpr bool GLOBAL_COMPRESSION_ACTUALLY_ENABLED = (GLOBAL_COMPRESSION_OPTIONS != uWS::DISABLED);

// uWebSockets 타입 전방 선언 (App.h에 이미 포함되어 있을 수 있음)
namespace uWS { 
    struct HttpRequest;
    template <bool SSL> struct HttpResponse;
}

namespace websocket_gateway { // WebSocketServer 클래스를 위한 네임스페이스

class WebSocketServer {
public:
    // PerSocketData는 types.h에 정의되어 있으며, websocket_gateway::STTClient를 사용해야 함
    using WebSocketConnection = uWS::WebSocket<GLOBAL_SSL_ENABLED, GLOBAL_COMPRESSION_ACTUALLY_ENABLED, PerSocketData>;

    WebSocketServer(int ws_port, int metrics_port, const std::string& stt_service_addr);
    ~WebSocketServer(); // 소멸자 선언

    bool run();
    void stop();
    WebSocketConnection* find_websocket_by_session_id(const std::string& session_id);

private:
    void initialize_handlers();
    std::string generate_session_id();

    // WebSocket 이벤트 핸들러
    void on_websocket_open(WebSocketConnection* ws);
    void on_websocket_message(WebSocketConnection* ws, std::string_view message, uWS::OpCode op_code);
    void on_websocket_close(WebSocketConnection* ws, int code, std::string_view message);
    
    // HTTP 라우트 핸들러
    void handle_health_check(uWS::HttpResponse<GLOBAL_SSL_ENABLED>* res, uWS::HttpRequest* req);
    void handle_metrics(uWS::HttpResponse<GLOBAL_SSL_ENABLED>* res, uWS::HttpRequest* req);

    // 멤버 변수
    int ws_port_;
    int metrics_port_;
    std::string stt_service_address_;

    uWS::TemplatedApp<GLOBAL_SSL_ENABLED> app_; // SSL 비활성화 시 false

    std::map<std::string, WebSocketConnection*> active_websockets_;
    std::mutex active_websockets_mutex_;

    std::atomic<long> connected_clients_count_{0};
    std::atomic<long> total_audio_bytes_processed_stt_{0};
    
    struct us_listen_socket_t *listen_socket_ws_ = nullptr; // uWebSockets 리슨 소켓
    std::atomic<bool> is_shutting_down_{false};
};

} // namespace websocket_gateway

#endif // WEBSOCKET_SERVER_H
