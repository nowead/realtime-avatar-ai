#ifndef TYPES_H
#define TYPES_H

#include <string>
#include <memory> // std::unique_ptr
// #include <chrono> // 유휴 시간 관리를 위해 필요시

// Forward declaration
class STTClient;

// uWebSockets의 각 연결에 대한 사용자 정의 데이터
struct PerSocketData {
    std::string sessionId;
    std::unique_ptr<STTClient> stt_client; // 세션별 STT 클라이언트
    bool stt_stream_active = false;
    // std::chrono::steady_clock::time_point last_activity; // 유휴 시간 관리를 위해
};

#endif // TYPES_H