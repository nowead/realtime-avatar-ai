#include <gtest/gtest.h>
#include "../src/tts_engine.h"
#include <fstream>
#include <filesystem>

// 🧪 파일로 저장하는 함수 테스트
TEST(TTSEngineTest, ShouldGenerateWavFileToDisk) {
    std::string text = "이것은 테스트용 문장입니다.";
    std::string path = "test_output.wav";

    bool success = run_open_tts_to_file(text, path);
    ASSERT_TRUE(success) << "OpenTTS 파일 저장 실패";

    // 파일이 생성되었는지 확인
    ASSERT_TRUE(std::filesystem::exists(path)) << "파일이 존재하지 않음";

    // 크기가 일정 이상인지 확인 (1KB 이상)
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    ASSERT_GT(file.tellg(), 1024) << "파일 크기가 너무 작음";

    // 정리
    std::filesystem::remove(path);
}

// 🧪 메모리로 반환하는 함수 테스트
TEST(TTSEngineTest, ShouldGenerateWavDataInMemory) {
    std::string text = "이것은 메모리 테스트용 문장입니다.";
    
    std::vector<uint8_t> audio_data = run_open_tts_to_memory(text);

    // 비어있지 않아야 함
    ASSERT_FALSE(audio_data.empty()) << "오디오 데이터가 비어 있음";

    // WAV 헤더 확인 (RIFF)
    ASSERT_EQ(audio_data[0], 'R');
    ASSERT_EQ(audio_data[1], 'I');
    ASSERT_EQ(audio_data[2], 'F');
    ASSERT_EQ(audio_data[3], 'F');
}
