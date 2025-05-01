#pragma once

#include <string>
#include <functional>
#include <memory>
#include <mutex>

// Azure Speech SDK 헤더 포함
#include <speechapi_cxx.h>

namespace stt {

// 네임스페이스 충돌 방지를 위해 Azure SDK 네임스페이스 사용 명시
using namespace Microsoft::CognitiveServices::Speech;
using namespace Microsoft::CognitiveServices::Speech::Audio;

// 인식된 텍스트 청크를 전달하기 위한 콜백 함수 타입
// parameters: (인식된 텍스트, 최종 결과 여부)
using TextChunkCallback = std::function<void(const std::string&, bool)>;

// 전체 인식 과정의 완료 또는 오류를 알리기 위한 콜백 함수 타입
// parameters: (성공 여부, 오류 메시지)
using RecognitionCompletionCallback = std::function<void(bool, const std::string&)>;

class AzureSTTClient {
public:
    // 생성자: Azure 서비스 키와 지역 필요
    AzureSTTClient(const std::string& key, const std::string& region);
    ~AzureSTTClient();

    // 연속적인 음성 인식 시작
    // parameters: (인식 언어 코드, 텍스트 콜백, 완료 콜백)
    // returns: 초기화 성공 여부
    bool StartContinuousRecognition(
        const std::string& language,
        const TextChunkCallback& textCb,
        const RecognitionCompletionCallback& completionCb
    );

    // 외부로부터 오디오 청크를 받아 SDK로 푸시
    void PushAudioChunk(const uint8_t* data, size_t size);

    // 오디오 입력 종료를 알리고 인식 완료 대기
    void StopContinuousRecognition();

private:
    // SDK 이벤트 핸들러 메소드 (내부 구현용)
    void HandleRecognizing(const SpeechRecognitionEventArgs& e);
    void HandleRecognized(const SpeechRecognitionEventArgs& e);
    void HandleCanceled(const SpeechRecognitionCanceledEventArgs& e);
    void HandleSessionStarted(const SessionEventArgs& e);
    void HandleSessionStopped(const SessionEventArgs& e);

    std::string subscription_key_;
    std::string region_;

    std::shared_ptr<SpeechConfig> speech_config_;
    std::shared_ptr<PushAudioInputStream> push_stream_;
    std::shared_ptr<AudioConfig> audio_config_;
    std::shared_ptr<SpeechRecognizer> recognizer_;

    // 콜백 함수 저장용 멤버 변수
    TextChunkCallback text_chunk_callback_;
    RecognitionCompletionCallback completion_callback_;

    // 비동기 작업 및 콜백 접근 보호용 뮤텍스
    std::mutex callback_mutex_;
    std::promise<void> recognition_stopped_promise_; // 인식 종료 시그널용
    bool recognition_has_error_ = false; // 오류 발생 여부 플래그
    std::string last_error_message_;     // 마지막 오류 메시지
};

} // namespace stt