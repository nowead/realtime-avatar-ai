#include <gtest/gtest.h>
#include "types.h"
#include "stt_client.h"
#include "websocket_server.h"
#include "avatar_sync_service_impl.h"

// 기본 PerSocketData 구조체 초기 상태 검증
TEST(PerSocketDataTest, DefaultValues) {
    PerSocketData data;
    EXPECT_EQ(data.sessionId, "");
    EXPECT_FALSE(data.stt_stream_active);
    EXPECT_EQ(data.stt_client, nullptr);
}

// STTClient: 스트림 시작 전 WriteAudioChunk 호출 시 false 반환 확인
TEST(STTClientTest, WriteAudioChunkWithoutStart) {
    STTClient client("invalid_address");
    EXPECT_FALSE(client.WriteAudioChunk("audio_data"));
}

// WebSocketServer: 존재하지 않는 세션 ID 조회 시 nullptr 반환 확인
TEST(WebSocketServerTest, FindWebSocketBySessionIdReturnsNull) {
    WebSocketServer server(12345, 12345, "localhost:50051");
    EXPECT_EQ(server.find_websocket_by_session_id("nonexistent"), nullptr);
}

// AvatarSyncServiceImpl: 기본 생성자와 WebSocketFinder 기본 동작 검증
TEST(AvatarSyncServiceImplTest, ConstructorWithDefaultFinder) {
    // 기본 WebSocketFinder를 전달하여 객체가 생성되는지 확인
    AvatarSyncServiceImpl::WebSocketFinder finder;
    AvatarSyncServiceImpl service(finder);
    SUCCEED();
}

// Google Test 실행 진입점
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
