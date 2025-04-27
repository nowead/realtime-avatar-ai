// tests/avatar_sync_service_test.cpp

#include <gtest/gtest.h>
#include "avatar_sync_service.h"
#include "websocket_dispatcher.h"

// 테스트용 스텁 디스패처
class TestDispatcher : public WebSocketDispatcher {
public:
    bool send_called = false;
    std::string last_session;
    std::vector<uint8_t> last_audio;
    std::vector<avatar::Viseme> last_visemes;
    std::string last_format;
    bool send_result = true;  // 반환값 조절

    void registerSession(const std::string&, std::shared_ptr<void>) override {}
    void unregisterSession(const std::string&) override {}

    bool sendToClient(const std::string& session_id,
                      const std::vector<uint8_t>& audio_data,
                      const std::vector<avatar::Viseme>& visemes,
                      const std::string& format) override 
    {
        send_called    = true;
        last_session   = session_id;
        last_audio     = audio_data;
        last_visemes   = visemes;
        last_format    = format;
        return send_result;
    }
};

TEST(AvatarSyncServiceTest, SyncAvatar_Success) {
    auto dispatcher = std::make_shared<TestDispatcher>();
    dispatcher->send_result = true;

    AvatarSyncServiceImpl service(dispatcher);

    avatar::SyncRequest req;
    req.set_session_id("sess-123");
    req.set_format("wav");
    req.set_audio_data(std::string("\x01\x02\x03", 3));
    auto* v = req.add_visemes();
    v->set_timestamp_ms(55.5f);
    v->set_viseme_id("v_mid");

    avatar::SyncResponse resp;
    grpc::ServerContext ctx;
    auto status = service.SyncAvatar(&ctx, &req, &resp);

    EXPECT_TRUE(status.ok());
    EXPECT_TRUE(resp.success());
    EXPECT_EQ(resp.message(), "✅ Sent successfully");

    ASSERT_TRUE(dispatcher->send_called);
    EXPECT_EQ(dispatcher->last_session, "sess-123");
    EXPECT_EQ(dispatcher->last_format, "wav");
    ASSERT_EQ(dispatcher->last_audio.size(), 3);
    EXPECT_EQ(dispatcher->last_audio[0], 0x01);
    EXPECT_EQ(dispatcher->last_visemes.size(), 1);
    EXPECT_FLOAT_EQ(dispatcher->last_visemes[0].timestamp_ms(), 55.5f);
    EXPECT_EQ(dispatcher->last_visemes[0].viseme_id(), "v_mid");
}

TEST(AvatarSyncServiceTest, SyncAvatar_Failure) {
    auto dispatcher = std::make_shared<TestDispatcher>();
    dispatcher->send_result = false;

    AvatarSyncServiceImpl service(dispatcher);

    avatar::SyncRequest req;
    req.set_session_id("sess-FAIL");
    req.set_format("pcm");
    req.set_audio_data("data");
    // no visemes

    avatar::SyncResponse resp;
    grpc::ServerContext ctx;
    auto status = service.SyncAvatar(&ctx, &req, &resp);

    EXPECT_TRUE(status.ok());
    EXPECT_FALSE(resp.success());
    EXPECT_EQ(resp.message(), "❌ Failed to send to client.");
    EXPECT_TRUE(dispatcher->send_called);
}

 // custom main to avoid needing gtest_main library
 int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}