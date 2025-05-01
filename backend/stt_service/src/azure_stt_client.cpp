#include "azure_stt_client.h"
#include <iostream>
#include <stdexcept> // std::runtime_error
#include <future>    // std::promise, std::future

namespace stt {

// 생성자
AzureSTTClient::AzureSTTClient(const std::string& key, const std::string& region)
  : subscription_key_(key), region_(region), recognition_has_error_(false)
{
    // SpeechConfig 생성 및 기본 설정 (키, 지역)
    speech_config_ = SpeechConfig::FromSubscription(subscription_key_, region_);
    if (!speech_config_) {
        throw std::runtime_error("Failed to create SpeechConfig from subscription.");
    }
    // 필요시 추가 설정 가능 (예: 로깅)
    // speech_config_->SetProperty(PropertyId::Speech_LogFilename, "/tmp/azure_stt_sdk.log");
    // std::cout << "💡 Azure STT SDK logging enabled to /tmp/azure_stt_sdk.log" << std::endl;
}

// 소멸자
AzureSTTClient::~AzureSTTClient() {
    // 만약 인식이 아직 진행 중이라면 명시적으로 중지 시도
    if (recognizer_) {
        // 비동기 중지 호출 (결과 대기 X, 소멸자에서 긴 대기 방지)
        recognizer_->StopContinuousRecognitionAsync();
        // 콜백 연결 해제 시도 (선택적, recognizer 파괴 시 자동 처리될 수 있음)
        // recognizer_->Recognizing.DisconnectAll();
        // recognizer_->Recognized.DisconnectAll();
        // ... 다른 이벤트들도 ...
    }
    // Push stream 명시적 닫기 (선택적)
    if (push_stream_) {
        push_stream_->Close();
    }
    std::cout << "ℹ️ AzureSTTClient destroyed." << std::endl;
}

// 연속 인식 시작
bool AzureSTTClient::StartContinuousRecognition(
    const std::string& language,
    const TextChunkCallback& textCb,
    const RecognitionCompletionCallback& completionCb
) {
    try {
        // 이미 Recognizer가 있다면 중지 시도 (재시작 로직)
        if(recognizer_) {
            std::cerr << "⚠️ Recognizer already exists. Stopping previous session..." << std::endl;
            StopContinuousRecognition(); // 이전 세션 정리 시도
            recognizer_ = nullptr; // Recognizer 해제
            push_stream_ = nullptr; // 스트림도 해제
            audio_config_ = nullptr;
             std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 잠시 대기
        }


        // 1. 콜백 함수 저장
        text_chunk_callback_ = textCb;
        completion_callback_ = completionCb;
        if (!text_chunk_callback_ || !completion_callback_) {
             std::cerr << "❌ Error: Callbacks cannot be null." << std::endl;
             return false;
        }


        // 2. 상태 초기화
        recognition_has_error_ = false;
        last_error_message_.clear();
        recognition_stopped_promise_ = std::promise<void>(); // 새 Promise 객체 생성


        // 3. 오디오 입력 스트림 생성 (Push 방식)
        // 오디오 포맷은 Azure SDK 기본값 (16kHz, 16bit, mono PCM)을 가정
        push_stream_ = AudioInputStream::CreatePushStream();
        audio_config_ = AudioConfig::FromStreamInput(push_stream_);


        // 4. 언어 설정
        speech_config_->SetSpeechRecognitionLanguage(language);


        // 5. SpeechRecognizer 생성
        recognizer_ = SpeechRecognizer::FromConfig(speech_config_, audio_config_);
        if (!recognizer_) {
            std::cerr << "❌ Failed to create SpeechRecognizer." << std::endl;
            return false;
        }


        // 6. SDK 이벤트 핸들러 연결
        // std::bind 또는 람다 사용 가능, 여기서는 람다 사용
        recognizer_->Recognizing.Connect([this](const SpeechRecognitionEventArgs& e) {
            this->HandleRecognizing(e);
        });
        recognizer_->Recognized.Connect([this](const SpeechRecognitionEventArgs& e) {
            this->HandleRecognized(e);
        });
        recognizer_->Canceled.Connect([this](const SpeechRecognitionCanceledEventArgs& e) {
            this->HandleCanceled(e);
        });
        recognizer_->SessionStarted.Connect([this](const SessionEventArgs& e) {
            this->HandleSessionStarted(e);
        });
        recognizer_->SessionStopped.Connect([this](const SessionEventArgs& e) {
            this->HandleSessionStopped(e);
        });


        // 7. 비동기적으로 연속 인식 시작
        auto future = recognizer_->StartContinuousRecognitionAsync();
        // 시작 결과 확인 (즉시 반환되는 오류 체크)
        if (future.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
             std::cerr << "❌ Recognition start timed out." << std::endl;
             recognition_has_error_ = true; // 타임아웃도 오류로 간주
             last_error_message_ = "Recognition start timed out.";
             // 실패 시 완료 콜백 호출 시도 (completionCb는 유효하다고 가정)
             std::lock_guard<std::mutex> lock(callback_mutex_);
             completion_callback_(false, last_error_message_);
            return false;
        }
         // future.get() 호출하여 즉각적인 에러 확인 (예: 잘못된 키)
         future.get(); // 여기서 예외 발생 시 catch 블록에서 처리


        std::cout << "✅ Continuous recognition started for language: " << language << std::endl;
        return true;


    } catch (const std::exception& e) {
        std::cerr << "❌ Exception during StartContinuousRecognition: " << e.what() << std::endl;
         recognition_has_error_ = true;
         last_error_message_ = "Exception during start: " + std::string(e.what());
         // 실패 시 완료 콜백 호출 시도
         if(completion_callback_) { // 콜백 유효성 재확인
             std::lock_guard<std::mutex> lock(callback_mutex_);
             completion_callback_(false, last_error_message_);
         }
        return false;
    }
}

// 오디오 청크 주입
void AzureSTTClient::PushAudioChunk(const uint8_t* data, size_t size) {
    if (push_stream_) {
        // 데이터를 스트림에 씁니다.
        push_stream_->Write(data, size);
    } else {
        // Recognizer 시작 전에 호출된 경우 등 오류 상황 로깅
        // std::cerr << "⚠️ PushAudioChunk called but push_stream_ is null." << std::endl;
    }
}

// 연속 인식 중지 (오디오 입력 종료)
void AzureSTTClient::StopContinuousRecognition() {
    try {
        if (!recognizer_) {
             std::cerr << "ℹ️ StopContinuousRecognition called but recognizer is not active." << std::endl;
            return;
        }


        std::cout << "ℹ️ Stopping continuous recognition..." << std::endl;


        // 1. 오디오 스트림 닫기 (더 이상 오디오 입력 없음을 SDK에 알림)
        if (push_stream_) {
            push_stream_->Close();
            std::cout << "   PushAudioInputStream closed." << std::endl;
        }


        // 2. SDK에 인식 중지 요청 (남은 오디오 처리 완료 대기)
        auto future = recognizer_->StopContinuousRecognitionAsync();
         if (future.wait_for(std::chrono::seconds(10)) == std::future_status::timeout) { // 타임아웃 설정
             std::cerr << "⚠️ Recognition stop request timed out after 10s." << std::endl;
             // 타임아웃 발생 시에도 SessionStopped 이벤트 기다려봄
         } else {
             future.get(); // 즉각적인 에러 확인
             std::cout << "   StopContinuousRecognitionAsync completed." << std::endl;
         }


        // 3. SessionStopped 이벤트가 처리될 때까지 대기 (Promise 사용)
        // SessionStopped 핸들러에서 promise가 set_value() 될 때까지 여기서 블록됨
        auto stop_future = recognition_stopped_promise_.get_future();
        if (stop_future.wait_for(std::chrono::seconds(15)) == std::future_status::timeout) { // 타임아웃 추가
             std::cerr << "⚠️ Timed out waiting for SessionStopped event after 15s." << std::endl;
              // 타임아웃 시 강제로 완료 콜백 호출 시도 (오류 상태)
             if (!recognition_has_error_) { // 아직 오류 보고 안됐으면
                last_error_message_ = "Timed out waiting for SessionStopped event.";
                recognition_has_error_ = true;
                 std::lock_guard<std::mutex> lock(callback_mutex_);
                 if (completion_callback_) {
                     try {
                         completion_callback_(!recognition_has_error_, last_error_message_);
                     } catch (const std::exception& cb_ex) {
                          std::cerr << "❌ Exception in completion callback (timeout path): " << cb_ex.what() << std::endl;
                     }
                 }
             }
        } else {
             std::cout << "   Session stopped event processed." << std::endl;
        }


        // 4. Recognizer 객체 정리 (선택적, 소멸자에서도 처리)
        // recognizer_ = nullptr;
        // push_stream_ = nullptr;
        // audio_config_ = nullptr;


    } catch (const std::exception& e) {
        std::cerr << "❌ Exception during StopContinuousRecognition: " << e.what() << std::endl;
        // 오류 발생 시에도 완료 콜백 호출 시도
        if (!recognition_has_error_) {
             last_error_message_ = "Exception during stop: " + std::string(e.what());
             recognition_has_error_ = true;
              std::lock_guard<std::mutex> lock(callback_mutex_);
              if (completion_callback_) {
                 try {
                     completion_callback_(false, last_error_message_);
                 } catch (const std::exception& cb_ex) {
                     std::cerr << "❌ Exception in completion callback (stop exception path): " << cb_ex.what() << std::endl;
                 }
             }
        }
        // 에러 발생 시 Promise를 설정하여 대기 해제 (SessionStopped가 호출 안 될 수 있으므로)
        try { recognition_stopped_promise_.set_value(); } catch (const std::future_error&) {}
    }
}

// --- Private SDK Event Handlers ---

void AzureSTTClient::HandleRecognizing(const SpeechRecognitionEventArgs& e) {
    auto result = e.Result;
    if (result->Reason == ResultReason::RecognizingSpeech) {
        std::string text = result->Text;
        //std::cout << "   INTERIM: '" << text << "'" << std::endl; // 로그 레벨 조절 필요
        // 콜백 호출 (중간 결과: is_final = false)
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (text_chunk_callback_) {
            try {
                text_chunk_callback_(text, false);
            } catch (const std::exception& cb_ex) {
                 std::cerr << "❌ Exception in text_chunk_callback_ (Recognizing): " << cb_ex.what() << std::endl;
            }
        }
    }
    // 다른 Reason 처리 (예: NoMatch)는 필요에 따라 추가
}

void AzureSTTClient::HandleRecognized(const SpeechRecognitionEventArgs& e) {
    auto result = e.Result;
    if (result->Reason == ResultReason::RecognizedSpeech) {
        std::string text = result->Text;
        std::cout << "   FINALIZED: '" << text << "'" << std::endl;
        // 콜백 호출 (최종 결과: is_final = true)
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (text_chunk_callback_) {
             try {
                text_chunk_callback_(text, true);
             } catch (const std::exception& cb_ex) {
                 std::cerr << "❌ Exception in text_chunk_callback_ (Recognized): " << cb_ex.what() << std::endl;
             }
        }
    } else if (result->Reason == ResultReason::NoMatch) {
        std::cout << "   NOMATCH: Speech could not be recognized." << std::endl;
        // NoMatch 시 오류로 처리할지 여부 결정 필요
    }
    // 다른 Reason 처리 필요 시 추가
}

void AzureSTTClient::HandleCanceled(const SpeechRecognitionCanceledEventArgs& e) {
    std::lock_guard<std::mutex> lock(callback_mutex_); // Lock before accessing shared state
    std::cerr << "❌ CANCELED: Reason=" << (int)e.Reason;
    recognition_has_error_ = true; // 취소는 오류로 간주

    if (e.Reason == CancellationReason::Error) {
        last_error_message_ = "Error: Code=" + std::to_string((int)e.ErrorCode) +
                              ", Details=" + e.ErrorDetails;
        std::cerr << " ErrorCode=" << (int)e.ErrorCode << " ErrorDetails=" << e.ErrorDetails;
    } else {
         last_error_message_ = "CancellationReason: " + std::to_string((int)e.Reason);
    }
     std::cerr << std::endl;

    // 완료 콜백 호출 (실패 상태)
    if (completion_callback_) {
         try {
            completion_callback_(false, last_error_message_);
         } catch (const std::exception& cb_ex) {
             std::cerr << "❌ Exception in completion_callback_ (Canceled): " << cb_ex.what() << std::endl;
         }
    }
    // Stop 대기를 해제하기 위해 Promise 설정
     try { recognition_stopped_promise_.set_value(); } catch (const std::future_error& fe) {
          // 이미 설정된 경우 무시 (예: SessionStopped가 먼저 호출된 극단적 경우)
         // std::cerr << "ℹ️ Promise already set in HandleCanceled? " << fe.what() << std::endl;
     }
}

void AzureSTTClient::HandleSessionStarted(const SessionEventArgs& e) {
    std::cout << "   SESSION STARTED: SessionId=" << e.SessionId << std::endl;
}

void AzureSTTClient::HandleSessionStopped(const SessionEventArgs& e) {
     std::lock_guard<std::mutex> lock(callback_mutex_); // Lock before accessing shared state
    std::cout << "   SESSION STOPPED: SessionId=" << e.SessionId << std::endl;

    // 세션 종료 시 최종 완료 콜백 호출 (성공/실패 여부 확인)
    if (completion_callback_) {
         try {
            // recognition_has_error_ 플래그를 통해 최종 성공/실패 결정
            completion_callback_(!recognition_has_error_, last_error_message_);
         } catch (const std::exception& cb_ex) {
             std::cerr << "❌ Exception in completion_callback_ (SessionStopped): " << cb_ex.what() << std::endl;
         }
    }
    // Stop 대기를 해제하기 위해 Promise 설정
    try { recognition_stopped_promise_.set_value(); } catch (const std::future_error& fe) {
         // 이미 설정된 경우 무시 (예: Canceled가 먼저 호출된 경우)
         // std::cerr << "ℹ️ Promise already set in HandleSessionStopped? " << fe.what() << std::endl;
    }
}

} // namespace stt