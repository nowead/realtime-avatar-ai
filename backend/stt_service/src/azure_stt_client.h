#pragma once // 수정됨: 헤더 중복 포함 방지 지시어 추가

#include <atomic>
#include <mutex>
#include <functional>
#include <string>
#include <memory>
#include <future> // std::promise 사용 시 필요할 수 있음

// Azure SDK 관련 헤더들...
#include <speechapi_cxx.h> // 실제 사용하는 헤더 이름 확인 필요

namespace stt {

// 콜백 타입 정의
using TextChunkCallback = std::function<void(const std::string&, bool)>;
using RecognitionCompletionCallback = std::function<void(bool, const std::string&)>;

class AzureSTTClient {
public:
    // ---=[ 생성자 선언 추가 ]=---
    // main.cpp 에서 std::make_shared 로 호출 시 필요
    explicit AzureSTTClient(const std::string& key, const std::string& region); // 수정됨: 생성자 선언 추가
    ~AzureSTTClient(); // 소멸자 선언 (이미 존재)

    // 복사 방지 (cpp 파일 구현 참고하여 필요시 추가/수정)
    AzureSTTClient(const AzureSTTClient&) = delete;
    AzureSTTClient& operator=(const AzureSTTClient&) = delete;
    AzureSTTClient(AzureSTTClient&&) = default; // 이동 생성/대입은 기본 사용 가능
    AzureSTTClient& operator=(AzureSTTClient&&) = default;

    // ---=[ 기존 Public 멤버 함수 선언 ]=---
    bool StartContinuousRecognition(
        const std::string& language,
        const TextChunkCallback& text_chunk_callback,
        const RecognitionCompletionCallback& completion_callback);

    // cpp 파일에 구현된 다른 public 함수들의 선언도 여기에 있어야 함
    void PushAudioChunk(const uint8_t* data, size_t size);
    void StopContinuousRecognition();
    // ... 기타 필요한 public 함수 선언 ...


private:
    // ---=[ 멤버 변수 선언 (이전 제안과 동일, 실제 코드와 비교 확인 필요) ]=---
    std::string subscription_key_; // 생성자에서 사용됨
    std::string region_;           // 생성자에서 사용됨

    std::shared_ptr<Microsoft::CognitiveServices::Speech::SpeechConfig> speech_config_; // 생성자에서 사용됨
    std::shared_ptr<Microsoft::CognitiveServices::Speech::Audio::AudioConfig> audio_config_; // Start에서 사용됨
    std::shared_ptr<Microsoft::CognitiveServices::Speech::Audio::PushAudioInputStream> push_stream_;
    std::shared_ptr<Microsoft::CognitiveServices::Speech::SpeechRecognizer> recognizer_;

    TextChunkCallback text_chunk_callback_;
    RecognitionCompletionCallback completion_callback_;

    std::atomic<bool> recognition_active_{false};
    std::atomic<bool> recognition_has_error_{false};
    std::mutex client_mutex_;
    std::string last_error_message_; // 오류 메시지 저장용
    std::promise<void> recognition_stopped_promise_; // 비동기 중지 완료 신호용

    std::string current_language_; // 필요시 현재 언어 저장

    // ---=[ Private 멤버 함수 선언 (콜백 핸들러 등, cpp 파일 구현과 일치 확인) ]=---
    void HandleRecognizing(const Microsoft::CognitiveServices::Speech::SpeechRecognitionEventArgs& e);
    void HandleRecognized(const Microsoft::CognitiveServices::Speech::SpeechRecognitionEventArgs& e);
    void HandleCanceled(const Microsoft::CognitiveServices::Speech::SpeechRecognitionCanceledEventArgs& e);
    void HandleSessionStarted(const Microsoft::CognitiveServices::Speech::SessionEventArgs& e); // cpp 파일에 구현됨
    void HandleSessionStopped(const Microsoft::CognitiveServices::Speech::SessionEventArgs& e);

}; // class AzureSTTClient

} // namespace stt