#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <future>

// Azure Speech SDK 헤더
#include <speechapi_cxx.h>

// 생성된 proto 헤더
#include "tts.pb.h"         // SynthesisConfig 사용
#include "avatar_sync.pb.h" // VisemeData 사용

namespace tts {

// 오디오 청크 및 비정형 데이터 콜백 타입 정의
// 첫 번째 인자: 오디오 청크 (bytes)
// 두 번째 인자: 비정형 데이터 (VisemeData) 리스트
using AudioVisemeCallback = std::function<void(const std::vector<uint8_t>&, const std::vector<avatar_sync::VisemeData>&)>;
// TTS 합성이 완료되었을 때 호출될 콜백 (성공 여부, 오류 메시지)
using SynthesisCompletionCallback = std::function<void(bool, const std::string&)>;


class AzureTTSEngine {
public:
    explicit AzureTTSEngine(const std::string& key, const std::string& region);
    ~AzureTTSEngine();

    AzureTTSEngine(const AzureTTSEngine&) = delete;
    AzureTTSEngine& operator=(const AzureTTSEngine&) = delete;
    AzureTTSEngine(AzureTTSEngine&&) = default;
    AzureTTSEngine& operator=(AzureTTSEngine&&) = default;

    // TTS 엔진 초기화 (합성 설정 포함)
    bool InitializeSynthesis(const SynthesisConfig& config);

    // 텍스트로부터 음성 및 비정형 데이터 합성 시작
    // 이 함수는 비동기적으로 작동하며, 데이터가 생성될 때마다 audio_viseme_callback을 호출합니다.
    // 모든 합성이 완료되거나 오류 발생 시 completion_callback을 호출합니다.
    bool Synthesize(const std::string& text,
                      const AudioVisemeCallback& audio_viseme_callback,
                      const SynthesisCompletionCallback& completion_callback);

    // 현재 진행 중인 합성을 중단합니다 (필요시).
    void StopSynthesis();

private:
    std::string subscription_key_;
    std::string region_;
    SynthesisConfig current_synthesis_config_;

    std::shared_ptr<Microsoft::CognitiveServices::Speech::SpeechConfig> speech_config_;
    std::shared_ptr<Microsoft::CognitiveServices::Speech::SpeechSynthesizer> synthesizer_;

    AudioVisemeCallback audio_viseme_callback_;
    SynthesisCompletionCallback completion_callback_;

    std::atomic<bool> synthesis_active_{false};
    std::atomic<bool> synthesis_has_error_{false};
    std::mutex engine_mutex_;
    std::string last_error_message_;

    // Azure SDK 이벤트 핸들러
    void HandleSynthesisStarted(const Microsoft::CognitiveServices::Speech::SpeechSynthesisEventArgs& e);
    void HandleSynthesizing(const Microsoft::CognitiveServices::Speech::SpeechSynthesisEventArgs& e);
    void HandleSynthesisCompleted(const Microsoft::CognitiveServices::Speech::SpeechSynthesisEventArgs& e);
    void HandleVisemeReceived(const Microsoft::CognitiveServices::Speech::SpeechSynthesisVisemeEventArgs& e);

    // 타임스탬프 변환 헬퍼
    google::protobuf::Timestamp ConvertTicksToTimestamp(uint64_t ticks);
};

} // namespace tts