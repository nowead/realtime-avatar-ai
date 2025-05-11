#ifndef TYPES_H
#define TYPES_H

#include <string>
#include <memory> // std::unique_ptr

// STTClient의 전체 정의를 포함하도록 수정합니다.
// 이렇게 하면 PerSocketData 내의 std::unique_ptr<websocket_gateway::STTClient>가
// STTClient를 완전한 타입으로 인식할 수 있습니다.
#include "stt_client.h"

// uWebSockets의 각 연결에 대한 사용자 정의 데이터
struct PerSocketData {
    std::string sessionId;
    // ★ STTClient 타입을 네임스페이스 포함하여 명시 (stt_client.h에서 정의된 네임스페이스 사용)
    std::unique_ptr<websocket_gateway::STTClient> stt_client; 
    bool stt_stream_active = false;
    // std::chrono::steady_clock::time_point last_activity; // 유휴 시간 관리를 위해
};

#endif // TYPES_H
