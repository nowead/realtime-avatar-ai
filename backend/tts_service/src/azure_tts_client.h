#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <utility>
#include <functional>
#include <memory>

// --- Azure Speech SDK 헤더 포함 ---
// 전방 선언 대신 실제 헤더를 포함하여 SpeechConfig 등의 타입을 알 수 있도록 함
#include <speechapi_cxx.h>
// --- End Azure Speech SDK 헤더 포함 ---


// // Forward declaration (이제 필요 없음)
// namespace Microsoft { namespace CognitiveServices { namespace Speech {
// class SpeechSynthesizer;
// class SpeechConfig; // SpeechConfig도 필요했음
// }}}

namespace tts {

  // Viseme 정보를 담는 별칭 (ID, time_ms)
  using VisemeInfo = std::pair<int, uint64_t>;

  // 기존 단일 결과 구조체
  struct SynthesisResult {
    std::vector<uint8_t> audio;
    std::vector<VisemeInfo> visemes;
  };

  // --- 스트리밍용 콜백 함수 타입 정의 ---
  using AudioChunkCallback = std::function<void(const uint8_t*, size_t)>;
  using VisemeCallback = std::function<void(int, uint64_t)>;
  using StreamCompletionCallback = std::function<void(bool, const std::string&)>;


  class AzureTTSClient {
  public:
    AzureTTSClient(const std::string& key, const std::string& region);
    ~AzureTTSClient();

    SynthesisResult synthesize(const std::string& text,
                               const std::string& voice,
                               const std::string& format);

    void synthesizeStream(
        const std::string& text,
        const std::string& voice,
        const AudioChunkCallback& audioCb,
        const VisemeCallback& visemeCb,
        const StreamCompletionCallback& completionCb
    );

  private:
    std::string subscription_key_;
    std::string region_;
    // 이제 SpeechConfig 타입이 알려졌으므로 std::shared_ptr 사용 가능
    std::shared_ptr<Microsoft::CognitiveServices::Speech::SpeechConfig> speech_config_;
  };

} // namespace tts