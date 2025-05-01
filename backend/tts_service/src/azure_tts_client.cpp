#include "azure_tts_client.h"
#include <future>
#include <thread>
#include <stdexcept>
#include <vector>
#include <iostream>
#include <mutex>

using namespace Microsoft::CognitiveServices::Speech;
using namespace Microsoft::CognitiveServices::Speech::Audio;

namespace tts {

// 생성자
AzureTTSClient::AzureTTSClient(const std::string& key, const std::string& region)
  : subscription_key_(key), region_(region)
{
    // SpeechConfig는 재사용 가능하므로 미리 생성
    speech_config_ = SpeechConfig::FromSubscription(subscription_key_, region_);
    speech_config_->SetSpeechSynthesisOutputFormat(SpeechSynthesisOutputFormat::Raw16Khz16BitMonoPcm);
    speech_config_->SetProperty(PropertyId::SpeechServiceResponse_RequestSentenceBoundary, "true");

    // SDK 로그 활성화
    // speech_config_->SetProperty(PropertyId::Speech_LogFilename, "/tmp/azure_sdk.log");
    // std::cout << "💡 SDK logging enabled to /tmp/azure_sdk.log" << std::endl;
}

AzureTTSClient::~AzureTTSClient() {
}

// 스트리밍 합성 메소드
void AzureTTSClient::synthesizeStream(
    const std::string& text,
    const std::string& voice,
    const AudioChunkCallback& audioCb,
    const VisemeCallback& visemeCb,
    const StreamCompletionCallback& completionCb
) {
    try {
        // 각 스트림 요청에 대해 Synthesizer 인스턴스 생성
        // Config 객체는 멤버 변수 speech_config_ 재사용
        auto synthesizer = SpeechSynthesizer::FromConfig(speech_config_);

        // 음성 설정
        if (!voice.empty()) {
            // ---=[ 중요: Synthesizer 속성 설정 제거 ]=---
            // synthesizer->get_Properties().SetProperty(PropertyId::SpeechServiceResponse_RequestSentenceBoundary, "true"); // 이 줄 제거
            // ---=[ Synthesizer 속성 설정 제거 끝 ]=---

            // 음성 이름은 Config가 아닌 Synthesizer 프라퍼티에 직접 설정해야 할 수도 있음.
            // 하지만 보통은 Config에 설정하는 것이 일반적. 우선 Config 설정을 유지.
            // 만약 음성 변경이 안된다면 아래 주석 해제 및 Config 설정 부분 주석 처리 필요
             speech_config_->SetSpeechSynthesisVoiceName(voice); // Config에 설정

             if (voice.find("Neural") == std::string::npos ||
                (voice.rfind("en-", 0) != 0 && voice.rfind("ko-", 0) != 0 ))
            {
                std::cerr << "⚠️ Voice '" << voice << "' might not support Viseme output correctly." << std::endl;
            }
        } else {
             std::cerr << "⚠️ Voice name is empty, using default voice. Viseme might not work." << std::endl;
             // 기본 음성 사용 시 Config에서 명시적으로 제거 또는 기본값 설정 확인
             // speech_config_->SetSpeechSynthesisVoiceName(""); // 기본값 사용 명시? (문서 확인 필요)
        }


        // --- 이벤트 핸들러 연결 ---
        std::promise<void> synthesis_finished_promise;
        std::string error_message; // 에러 메시지 저장용

        // 1. 오디오 데이터 청크 수신
        synthesizer->Synthesizing.Connect([&audioCb](const SpeechSynthesisEventArgs& e) {
            auto audio_data = e.Result->GetAudioData();
            if (audio_data && !audio_data->empty()) { // size() 대신 empty() 사용 가능
                 if(audioCb) {
                    audioCb(audio_data->data(), audio_data->size());
                 }
            }
        });

        // 2. Viseme 정보 수신
        synthesizer->VisemeReceived.Connect(
          [&visemeCb](const SpeechSynthesisVisemeEventArgs& e) {
             if (visemeCb) {
                 visemeCb(static_cast<int>(e.VisemeId),
                          static_cast<uint64_t>(e.AudioOffset / 10000)); // ms 단위로 변환
             }
          }
        );

        // 3. 합성 완료 또는 취소 처리
        auto handle_completion = [&](const SpeechSynthesisEventArgs& e) {
             bool success = false; // 성공 여부 플래그
            if (e.Result->Reason == ResultReason::SynthesizingAudioCompleted) {
                success = true;
            } else {
                auto cancellation = SpeechSynthesisCancellationDetails::FromResult(e.Result);
                error_message = "Synthesis failed/cancelled: Reason=" + std::to_string((int)cancellation->Reason);
                if (cancellation->Reason == CancellationReason::Error) {
                    error_message += ", ErrorCode=" + std::to_string((int)cancellation->ErrorCode);
                    error_message += ", ErrorDetails=[" + cancellation->ErrorDetails + "]";
                }
                 std::cerr << "❌ " << error_message << std::endl;
            }
             // set_value는 한 번만 호출되어야 하므로, promise는 여기서 완료 처리
             // 콜백 내에서 예외 발생 가능성 줄이기 위해 set_value 사용
             try {
                 synthesis_finished_promise.set_value(); // 성공/실패 무관하게 promise 완료
             } catch (const std::future_error& e) {
                 // 이미 set된 경우 무시 (예: Canceled 와 Completed 가 동시에? 발생 가능성은 낮음)
                 std::cerr << "ℹ️ Promise already set: " << e.what() << std::endl;
             }
        };

        // SynthesisCompleted 와 SynthesisCanceled 이벤트 핸들러 연결
        synthesizer->SynthesisCompleted.Connect(handle_completion);
        synthesizer->SynthesisCanceled.Connect(handle_completion);


        // --- SSML 구성 ---
        // 언어 코드 설정 확인 필요 (예: ko-KR)
         std::string lang = "ko-KR"; // 기본 한국어 설정
         if (!voice.empty() && voice.length() > 5) {
             lang = voice.substr(0, 5); // voice 이름에서 언어 코드 추출 (예: ko-KR-)
         }

        std::string ssml =
          "<speak version='1.0' "
            "xmlns='http://www.w3.org/2001/10/synthesis' "
            "xmlns:mstts='http://www.w3.org/2001/mstts' "
            "xml:lang='" + lang +"'>" // 언어 코드 적용
            "<voice name='" + voice + "'>"
              "<mstts:viseme type='redlips_front'/>"
                 + text +
            "</voice>"
          "</speak>";


        // --- 비동기 합성 시작 ---
        auto future = synthesizer->StartSpeakingSsmlAsync(ssml);

        // 시작 결과 확인
        if (future.wait_for(std::chrono::seconds(10)) == std::future_status::timeout) { // 타임아웃 증가
             error_message = "TTS start timed out after 10 seconds.";
             std::cerr << "❌ " << error_message << std::endl;
             // 시작 실패 시에도 promise 완료 및 콜백 호출 필요
              try { synthesis_finished_promise.set_value(); } catch (const std::future_error&) {}
        } else {
             auto result = future.get();
             if (result->Reason != ResultReason::SynthesizingAudioStarted) {
                 auto cancellation = SpeechSynthesisCancellationDetails::FromResult(result);
                 error_message = "TTS start failed, Reason=" + std::to_string(static_cast<int>(result->Reason));
                 if (cancellation) {
                      error_message += ", Details: " + cancellation->ErrorDetails;
                 }
                 std::cerr << "❌ " << error_message << std::endl;
                 // 시작 실패 시에도 promise 완료 및 콜백 호출 필요
                 try { synthesis_finished_promise.set_value(); } catch (const std::future_error&) {}
             }
        }

        // --- 합성 완료 대기 및 콜백 호출 ---
        auto completion_future = synthesis_finished_promise.get_future();
        completion_future.wait(); // 백그라운드 합성 완료 대기 (handle_completion 에서 set_value 호출될 때까지)

        // 완료 콜백 호출
        if (completionCb) {
            // error_message 가 비어 있으면 성공
            completionCb(error_message.empty(), error_message);
        }


    } catch (const std::exception& e) {
        std::cerr << "❌ Exception in synthesizeStream: " << e.what() << std::endl;
        if (completionCb) {
            completionCb(false, "Exception caught: " + std::string(e.what()));
        }
    } catch (...) {
        std::cerr << "❌ Unknown exception in synthesizeStream." << std::endl;
         if (completionCb) {
            completionCb(false, "Unknown exception caught.");
         }
    }
}


} // namespace tts