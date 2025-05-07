// tests/internal_unit_tests.cpp for TTS Service

#include <gtest/gtest.h>
#include <string>
#include <stdexcept> // For std::runtime_error
#include <vector>
#include <atomic>
#include <condition_variable> // For waiting on callbacks
#include <thread>   // std::this_thread::sleep_for 사용을 위해
#include <chrono>   // std::chrono::milliseconds 사용을 위해

// 헤더 파일 경로 주의 (CMake 설정에 따라 달라질 수 있음)
#include "azure_tts_engine.h"     // 테스트 대상
#include "avatar_sync_client.h" // AvatarSyncClient 생성자 등 테스트용
#include "tts.pb.h"             // SynthesisConfig 사용

// AzureTTSEngine 테스트를 위한 환경 변수 (실제 키/지역 필요, CI에서는 Mock 사용 권장)
const char* azure_key_env_test = std::getenv("AZURE_SPEECH_KEY_TEST");
const char* azure_region_env_test = std::getenv("AZURE_SPEECH_REGION_TEST");

// --- AvatarSyncClient Constructor Test ---
TEST(TTSInternalClientTest, AvatarSyncClientConstructor_InvalidAddress) {
    // 빈 주소로 AvatarSyncClient 생성 시 예외가 발생하지 않아야 함 (연결 시 실패)
    EXPECT_NO_THROW(tts::AvatarSyncClient("")); // 빈 주소는 채널 생성 시 예외 안 던짐
    EXPECT_NO_THROW(tts::AvatarSyncClient("localhost:12345"));
}


// --- AzureTTSEngine Tests ---
class AzureTTSEngineTest : public ::testing::Test {
protected:
    std::string valid_key;
    std::string valid_region;
    bool credentials_available = false;

    void SetUp() override {
        if (azure_key_env_test && azure_region_env_test) {
            valid_key = azure_key_env_test;
            valid_region = azure_region_env_test;
            if (!valid_key.empty() && !valid_region.empty()) {
                credentials_available = true;
            }
        }
        if (!credentials_available) {
            std::cout << "WARN: AZURE_SPEECH_KEY_TEST or AZURE_SPEECH_REGION_TEST env vars not set. Skipping some AzureTTSEngine tests." << std::endl;
        }
    }
};

TEST_F(AzureTTSEngineTest, Constructor_MissingCredentials) {
    EXPECT_ANY_THROW(tts::AzureTTSEngine("", "mock_region"));
    EXPECT_ANY_THROW(tts::AzureTTSEngine("mock_key", ""));
    EXPECT_ANY_THROW(tts::AzureTTSEngine("", ""));
}

TEST_F(AzureTTSEngineTest, Constructor_ValidCredentials) {
    if (!credentials_available) {
        GTEST_SKIP() << "Skipping test, Azure credentials not available.";
    }
    EXPECT_NO_THROW(tts::AzureTTSEngine(valid_key, valid_region));
}

TEST_F(AzureTTSEngineTest, InitializeSynthesis_ValidConfig) {
    if (!credentials_available) {
        GTEST_SKIP() << "Skipping test, Azure credentials not available.";
    }
    tts::AzureTTSEngine engine(valid_key, valid_region);
    tts::SynthesisConfig config;
    config.set_language_code("en-US"); // 실제 지원 언어
    config.set_voice_name("en-US-JennyNeural"); // 실제 지원 음성
    config.set_session_id("test-session-init");

    EXPECT_TRUE(engine.InitializeSynthesis(config));
}

TEST_F(AzureTTSEngineTest, InitializeSynthesis_InvalidConfig) {
    if (!credentials_available) {
        GTEST_SKIP() << "Skipping test, Azure credentials not available.";
    }
    tts::AzureTTSEngine engine(valid_key, valid_region);
    tts::SynthesisConfig config_no_lang;
    config_no_lang.set_voice_name("en-US-JennyNeural");
    EXPECT_FALSE(engine.InitializeSynthesis(config_no_lang));

    tts::SynthesisConfig config_no_voice;
    config_no_voice.set_language_code("en-US");
    EXPECT_FALSE(engine.InitializeSynthesis(config_no_voice));
}

TEST_F(AzureTTSEngineTest, Synthesize_BasicFlow) {
    if (!credentials_available) {
        GTEST_SKIP() << "Skipping test, Azure credentials not available.";
    }
    tts::AzureTTSEngine engine(valid_key, valid_region);
    tts::SynthesisConfig config;
    config.set_language_code("ko-KR");
    config.set_voice_name("ko-KR-SunHiNeural"); // 한국어 음성
    config.set_session_id("test-session-synth");
    ASSERT_TRUE(engine.InitializeSynthesis(config));

    std::string test_text = "안녕하세요. 이것은 유닛 테스트입니다.";
    std::atomic<bool> audio_received{false};
    std::atomic<bool> viseme_received{false};
    std::atomic<bool> completion_called{false};
    std::atomic<bool> synthesis_success{false};
    std::mutex cv_m;
    std::condition_variable cv;

    auto audio_viseme_cb = [&](const std::vector<uint8_t>& audio_chunk, const std::vector<avatar_sync::VisemeData>& visemes) {
        if (!audio_chunk.empty()) audio_received.store(true);
        if (!visemes.empty()) viseme_received.store(true);
    };

    auto completion_cb = [&](bool success, const std::string& msg) {
        std::lock_guard<std::mutex> lk(cv_m);
        synthesis_success.store(success);
        completion_called.store(true);
        if (!success) {
            std::cout << "Test Synthesis Error: " << msg << std::endl;
        }
        cv.notify_one();
    };

    ASSERT_TRUE(engine.Synthesize(test_text, audio_viseme_cb, completion_cb));

    // 콜백 완료 대기 (타임아웃 설정)
    std::unique_lock<std::mutex> lk(cv_m);
    bool wait_result = cv.wait_for(lk, std::chrono::seconds(15), [&]{ return completion_called.load(); });

    EXPECT_TRUE(wait_result) << "Timeout waiting for synthesis completion callback.";
    EXPECT_TRUE(completion_called.load());
    EXPECT_TRUE(synthesis_success.load());
    EXPECT_TRUE(audio_received.load());
    EXPECT_TRUE(viseme_received.load()); // Azure TTS는 Viseme 이벤트 제공
}

TEST_F(AzureTTSEngineTest, Synthesize_EmptyText) {
    if (!credentials_available) {
        GTEST_SKIP() << "Skipping test, Azure credentials not available.";
    }
    tts::AzureTTSEngine engine(valid_key, valid_region);
    tts::SynthesisConfig config;
    config.set_language_code("en-US");
    config.set_voice_name("en-US-JennyNeural");
    ASSERT_TRUE(engine.InitializeSynthesis(config));

    std::atomic<bool> completion_called{false};
    std::atomic<bool> success_status{false};

    // 빈 텍스트는 오류 없이 성공적으로 완료되어야 함 (오디오/비정형은 없을 것)
    EXPECT_TRUE(engine.Synthesize("",
        [](const auto& ac, const auto& vd){ FAIL() << "Callback should not be called for empty text."; },
        [&](bool success, const std::string& msg){
            completion_called.store(true);
            success_status.store(success);
        }
    ));
    // AzureTTSEngine의 Synthesize("")는 즉시 true를 반환하고 completion_cb를 호출할 수 있음.
    // 혹은 아무 작업도 안하고 true만 반환할 수도 있음 (구현에 따라 다름).
    // 현재 AzureTTSEngine 구현은 빈 텍스트 시 completion_cb(true, "") 호출.
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 콜백 호출 시간 확보
    EXPECT_TRUE(completion_called.load());
    EXPECT_TRUE(success_status.load());
}


// --- Main Function ---
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    std::cout << "Running TTS internal unit tests..." << std::endl;
    return RUN_ALL_TESTS();
}