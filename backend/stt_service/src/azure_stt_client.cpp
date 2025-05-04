// azure_stt_client.cpp (수정된 전체 코드)

#include "azure_stt_client.h" // 자신의 헤더 포함 필수
#include <iostream>
#include <stdexcept>
#include <future>
#include <thread> // for this_thread
#include <chrono> // for milliseconds
#include <functional> // for std::function comparisons

// ---=[ 수정됨: Azure Speech SDK 네임스페이스 사용 선언 추가 ]=---
using namespace Microsoft::CognitiveServices::Speech;
using namespace Microsoft::CognitiveServices::Speech::Audio;
// ---=[ 수정 끝 ]=---


namespace stt {

// 생성자
AzureSTTClient::AzureSTTClient(const std::string& key, const std::string& region)
  : subscription_key_(key), region_(region)
{
    try {
        // 수정됨: 네임스페이스 사용으로 타입 이름만 사용 가능
        speech_config_ = SpeechConfig::FromSubscription(subscription_key_, region_);
        if (!speech_config_) {
            throw std::runtime_error("Failed to create SpeechConfig from subscription. Check key and region.");
        }
        // 필요시 추가 설정 ...
    } catch (const std::exception& e) {
        std::cerr << "❌ Exception in AzureSTTClient constructor: " << e.what() << std::endl;
        throw;
    }
    std::cout << "  AzureSTTClient initialized for region: " << region_ << std::endl;
}

// 소멸자
AzureSTTClient::~AzureSTTClient() {
    std::cout << "ℹ️ Destroying AzureSTTClient..." << std::endl;
    if (recognition_active_.load()) {
        std::cerr << "⚠️ WARNING: AzureSTTClient destroyed while recognition was still active. Attempting to stop..." << std::endl;
        try {
            if (recognizer_) {
                 std::cout << "   Stopping recognition asynchronously..." << std::endl;
                 recognizer_->StopContinuousRecognitionAsync().get();
            }
            if (push_stream_) {
                push_stream_->Close();
            }
            // 콜백 연결 해제 (필요시)
            // if(recognizer_) { ... recognizer_->Recognizing.DisconnectAll(); ... }
        } catch (const std::exception& e) {
             std::cerr << "   Exception during cleanup in destructor: " << e.what() << std::endl;
        }
    }
    recognizer_.reset();
    audio_config_.reset();
    push_stream_.reset();
    speech_config_.reset();
    std::cout << "✅ AzureSTTClient destroyed." << std::endl;
}

// 연속 인식 시작 (시그니처 및 내부 로직 수정됨 - 이전과 동일하게 유지)
bool AzureSTTClient::StartContinuousRecognition(
    const std::string& language,
    const TextChunkCallback& textCb,           // 수정됨: const& 사용 (헤더와 일치)
    const RecognitionCompletionCallback& completionCb // 수정됨: const& 사용 (헤더와 일치)
) {
    std::lock_guard<std::mutex> lock(client_mutex_);

    if (recognition_active_.load()) {
        std::cerr << "⚠️ StartContinuousRecognition called while already active. Stop previous session first." << std::endl;
        return false;
    }

    std::cout << "⏳ Starting Azure continuous recognition for language: " << language << std::endl;

    try {
        recognizer_.reset();
        audio_config_.reset();
        push_stream_.reset();

        // 1. 콜백 함수 저장 (const& 이므로 복사 대입)
        text_chunk_callback_ = textCb;           // 수정됨: 복사 대입
        completion_callback_ = completionCb;     // 수정됨: 복사 대입
        if (!text_chunk_callback_ || !completion_callback_) {
             std::cerr << "❌ Error: Invalid callbacks provided (null function)." << std::endl;
             return false;
        }

        // 2. 상태 초기화
        recognition_has_error_.store(false);
        last_error_message_.clear();
        recognition_stopped_promise_ = std::promise<void>();

        // 3. 오디오 입력 스트림 생성
        push_stream_ = AudioInputStream::CreatePushStream();
        if (!push_stream_) {
             throw std::runtime_error("Failed to create push audio input stream.");
        }
        audio_config_ = AudioConfig::FromStreamInput(push_stream_);
         if (!audio_config_) {
             throw std::runtime_error("Failed to create audio config from stream input.");
        }

        // 4. 언어 설정
        speech_config_->SetSpeechRecognitionLanguage(language);
        current_language_ = language;

        // 5. SpeechRecognizer 생성
        recognizer_ = SpeechRecognizer::FromConfig(speech_config_, audio_config_);
        if (!recognizer_) {
            throw std::runtime_error("Failed to create SpeechRecognizer.");
        }

        // 6. SDK 이벤트 핸들러 연결 (using namespace 로 인해 타입 이름만 사용 가능)
        recognizer_->Recognizing.Connect([this](const SpeechRecognitionEventArgs& e) { this->HandleRecognizing(e); });
        recognizer_->Recognized.Connect([this](const SpeechRecognitionEventArgs& e) { this->HandleRecognized(e); });
        recognizer_->Canceled.Connect([this](const SpeechRecognitionCanceledEventArgs& e) { this->HandleCanceled(e); });
        recognizer_->SessionStarted.Connect([this](const SessionEventArgs& e) { this->HandleSessionStarted(e); });
        recognizer_->SessionStopped.Connect([this](const SessionEventArgs& e) { this->HandleSessionStopped(e); });

        // 7. 비동기적으로 연속 인식 시작
        auto start_future = recognizer_->StartContinuousRecognitionAsync();

        std::future_status status = start_future.wait_for(std::chrono::seconds(5));
        if (status == std::future_status::timeout) {
             throw std::runtime_error("Recognition start timed out.");
        }
        start_future.get();

        recognition_active_.store(true);
        std::cout << "✅ Azure continuous recognition successfully started." << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "❌ Exception during StartContinuousRecognition: " << e.what() << std::endl;
        recognizer_.reset();
        audio_config_.reset();
        push_stream_.reset();
        recognition_active_.store(false);
        return false;
    }
}

// 오디오 청크 주입 (const_cast 추가됨 - 이전과 동일하게 유지)
void AzureSTTClient::PushAudioChunk(const uint8_t* data, size_t size) {
    if (recognition_active_.load() && push_stream_) {
        // SDK Write 함수가 non-const 포인터를 요구하는 문제 해결 (이전 오류 로그 기반)
        push_stream_->Write(const_cast<uint8_t*>(data), static_cast<uint32_t>(size)); // 수정됨: const_cast 및 타입 캐스팅
    }
}

// 연속 인식 중지 (변경 없음 - 헤더 변수 선언 및 타입이 올바르다면)
void AzureSTTClient::StopContinuousRecognition() {
    if (!recognition_active_.load()) {
        std::cout << "ℹ️ StopContinuousRecognition called but recognition is not active." << std::endl;
        return;
    }
    std::cout << "⏳ Stopping Azure continuous recognition..." << std::endl;
    try {
        if (push_stream_) {
             std::cout << "   Closing push audio stream..." << std::endl;
            push_stream_->Close();
        }
        if (recognizer_) {
             std::cout << "   Requesting stop continuous recognition..." << std::endl;
            auto stop_future = recognizer_->StopContinuousRecognitionAsync();
            std::future_status status = stop_future.wait_for(std::chrono::seconds(10));
            if (status == std::future_status::timeout) {
                std::cerr << "⚠️ Recognition stop request timed out (10s). Session might stop later." << std::endl;
            } else {
                stop_future.get();
                 std::cout << "   Stop request acknowledged by SDK." << std::endl;
            }
        } else {
             std::cerr << "⚠️ Recognizer is null during stop request?" << std::endl;
             try { recognition_stopped_promise_.set_value(); } catch (const std::future_error&) {}
             recognition_active_.store(false);
             return;
        }
         std::cout << "   Waiting for SessionStopped event..." << std::endl;
        auto final_stop_future = recognition_stopped_promise_.get_future();
        if (final_stop_future.wait_for(std::chrono::seconds(20)) == std::future_status::timeout) {
            std::cerr << "⚠️ Timed out waiting for SessionStopped event (20s)." << std::endl;
             std::lock_guard<std::mutex> lock(client_mutex_);
            if (recognition_active_.load()) {
                 recognition_has_error_.store(true);
                 last_error_message_ = "Timed out waiting for session stop.";
            }
             recognition_active_.store(false);
        } else {
             std::cout << "   SessionStopped event received or promise fulfilled." << std::endl;
        }
        recognition_active_.store(false);
    } catch (const std::exception& e) {
        std::cerr << "❌ Exception during StopContinuousRecognition: " << e.what() << std::endl;
        std::lock_guard<std::mutex> lock(client_mutex_);
        recognition_has_error_.store(true);
        last_error_message_ = "Exception during stop: " + std::string(e.what());
        recognition_active_.store(false);
        try { recognition_stopped_promise_.set_value(); } catch (const std::future_error&) {}
    }
    std::cout << "✅ StopContinuousRecognition sequence finished." << std::endl;
}

// --- Private SDK Event Handlers ---
// (using namespace 추가로 인해 타입 이름만 사용 가능)

// HandleRecognizing (using namespace 적용 외 변경 없음)
void AzureSTTClient::HandleRecognizing(const SpeechRecognitionEventArgs& e) { // 수정됨: 타입 이름만 사용
    std::string text;
    bool callback_needed = false;
    if (e.Result->Reason == ResultReason::RecognizingSpeech) {
        text = e.Result->Text;
        if (!text.empty()) {
            callback_needed = true;
        }
    }
    if (callback_needed) {
        TextChunkCallback cb; { std::lock_guard<std::mutex> lock(client_mutex_); cb = text_chunk_callback_; }
        if (cb) {
            try { cb(text, false); } catch (const std::exception& cb_ex) {
                std::cerr << "❌ Exception in text_chunk_callback_ (Recognizing): " << cb_ex.what() << std::endl;
            }
        }
    }
}

// HandleRecognized (using namespace 적용 외 변경 없음)
void AzureSTTClient::HandleRecognized(const SpeechRecognitionEventArgs& e) { // 수정됨: 타입 이름만 사용
    std::string text;
    bool callback_needed = false;
    ResultReason reason = e.Result->Reason;
    if (reason == ResultReason::RecognizedSpeech) {
        text = e.Result->Text;
         std::cout << "   FINALIZED: '" << text << "'" << std::endl;
         if (!text.empty()) { callback_needed = true; }
    } else if (reason == ResultReason::NoMatch) {
        std::cout << "   NOMATCH: Speech could not be recognized." << std::endl;
    }
    if (callback_needed) {
        TextChunkCallback cb; { std::lock_guard<std::mutex> lock(client_mutex_); cb = text_chunk_callback_; }
        if (cb) {
             try { cb(text, true); } catch (const std::exception& cb_ex) {
                 std::cerr << "❌ Exception in text_chunk_callback_ (Recognized): " << cb_ex.what() << std::endl;
             }
        }
    }
}

// HandleCanceled (using namespace 적용 외 변경 없음)
void AzureSTTClient::HandleCanceled(const SpeechRecognitionCanceledEventArgs& e) { // 수정됨: 타입 이름만 사용
    std::string error_details;
    std::string cancellation_reason_str;
    bool is_error = false;
    RecognitionCompletionCallback cb;
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        recognition_active_.store(false);
        recognition_has_error_.store(true);
        cancellation_reason_str = std::to_string(static_cast<int>(e.Reason));
        std::cerr << "❌ CANCELED: Reason=" << cancellation_reason_str;
        if (e.Reason == CancellationReason::Error) {
            is_error = true;
            error_details = "ErrorCode=" + std::to_string(static_cast<int>(e.ErrorCode)) + ", Details=" + e.ErrorDetails;
            std::cerr << " " << error_details;
        }
        std::cerr << std::endl;
        last_error_message_ = "CancellationReason: " + cancellation_reason_str + (is_error ? " " + error_details : "");
        cb = completion_callback_;
        try { recognition_stopped_promise_.set_value(); } catch (const std::future_error&) {}
    }
    if (cb) {
        try { cb(false, last_error_message_); } catch (const std::exception& cb_ex) {
            std::cerr << "❌ Exception in completion_callback_ (Canceled): " << cb_ex.what() << std::endl;
        }
    }
}

// HandleSessionStarted (using namespace 적용 외 변경 없음)
void AzureSTTClient::HandleSessionStarted(const SessionEventArgs& e) { // 수정됨: 타입 이름만 사용
    std::cout << "   SESSION STARTED: SessionId=" << e.SessionId << std::endl;
}

// HandleSessionStopped (using namespace 적용 외 변경 없음)
void AzureSTTClient::HandleSessionStopped(const SessionEventArgs& e) { // 수정됨: 타입 이름만 사용
    bool final_success = false;
    std::string final_message;
    RecognitionCompletionCallback cb;
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        std::cout << "   SESSION STOPPED: SessionId=" << e.SessionId << std::endl;
        recognition_active_.store(false);
        final_success = !recognition_has_error_.load();
        final_message = last_error_message_;
        cb = completion_callback_;
        try { recognition_stopped_promise_.set_value(); } catch (const std::future_error&) {}
    }
    if (cb) {
        try { cb(final_success, final_message); } catch (const std::exception& cb_ex) {
            std::cerr << "❌ Exception in completion_callback_ (SessionStopped): " << cb_ex.what() << std::endl;
        }
    } else {
         std::cerr << "⚠️ Completion callback is null in SessionStopped handler." << std::endl;
    }
}

} // namespace stt