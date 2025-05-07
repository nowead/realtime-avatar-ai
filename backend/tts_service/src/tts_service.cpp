#include "tts_service.h"
#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <future> // std::promise, std::future 사용

using grpc::Status;
using grpc::StatusCode;

namespace tts {

std::string TTSServiceImpl::generate_uuid() {
    // ... (기존 UUID 생성 코드) ...
     std::random_device rd;
     std::mt19937_64 gen(rd());
     std::uniform_int_distribution<uint64_t> dis;
     std::stringstream ss;
     ss << std::hex << std::setfill('0');
     ss << std::setw(16) << dis(gen);
     ss << std::setw(16) << dis(gen);
     return ss.str();
}


TTSServiceImpl::TTSServiceImpl(std::shared_ptr<AvatarSyncClient> avatar_sync_client,
                                 std::function<std::unique_ptr<AzureTTSEngine>()> tts_engine_factory)
  : avatar_sync_client_(avatar_sync_client), tts_engine_factory_(tts_engine_factory) {
    if (!avatar_sync_client_) {
        throw std::runtime_error("AvatarSyncClient cannot be null in TTSServiceImpl.");
    }
    if (!tts_engine_factory_) {
        throw std::runtime_error("TTS Engine factory cannot be null in TTSServiceImpl.");
    }
    std::cout << "TTSServiceImpl created. Thread ID: " << std::this_thread::get_id() << std::endl;
}

TTSServiceImpl::~TTSServiceImpl() {
    std::cout << "TTSServiceImpl destroyed. Thread ID: " << std::this_thread::get_id() << std::endl;
}

Status TTSServiceImpl::SynthesizeStream(
    ServerContext* context,
    ServerReader<TTSStreamRequest>* reader,
    Empty* response) {

    const std::string client_peer = context->peer();
    std::string session_id;
    std::cout << "✅ New LLM client connection for TTS from: " << client_peer
              << " (Thread ID: " << std::this_thread::get_id() << ")" << std::endl;

    std::unique_ptr<AzureTTSEngine> tts_engine = nullptr;
    SynthesisConfig active_synthesis_config;
    bool tts_engine_initialized = false;
    bool avatar_sync_stream_started = false;
    std::atomic<bool> synthesis_error_occurred{false};
    std::string error_message_detail;

    // 전체 스트림 완료를 위한 Promise (기존 로직 유지)
    std::promise<void> overall_synthesis_complete_promise;
    auto overall_synthesis_complete_future = overall_synthesis_complete_promise.get_future();

    auto cleanup_resources = [&](const std::string& current_session_id_ref) {
        // ... (기존 cleanup_resources 코드, 수정 없음) ...
         std::cout << "🧹 [" << (current_session_id_ref.empty() ? "NO_SESSION" : current_session_id_ref)
                   << "] Cleaning up TTS resources..." << std::endl;

         if (avatar_sync_stream_started && avatar_sync_client_ && avatar_sync_client_->IsStreamActive()) {
             std::cout << "   Finishing AvatarSync stream for session [" << current_session_id_ref << "]..." << std::endl;
             Status avatar_finish_status = avatar_sync_client_->FinishStream();
             if (!avatar_finish_status.ok()) {
                 std::cerr << "   ⚠️ AvatarSync stream finish error during cleanup: ("
                           << avatar_finish_status.error_code() << ") "
                           << avatar_finish_status.error_message() << std::endl;
             } else {
                 std::cout << "   AvatarSync stream finished successfully during cleanup." << std::endl;
             }
         }
         avatar_sync_stream_started = false;

         if (tts_engine_initialized && tts_engine) {
             std::cout << "   Stopping/Finalizing TTS engine for session [" << current_session_id_ref << "]..." << std::endl;
             tts_engine->StopSynthesis();
         }
         tts_engine_initialized = false;
         tts_engine.reset();
    };

    try {
        TTSStreamRequest request;
        bool first_message = true;

        // 콜백 정의 (반복문 외부에서 정의, 필요시 캡처 수정)
        // audio_viseme 콜백은 상태 변경이 적으므로 외부 정의 가능
        auto audio_viseme_cb =
            [this, &synthesis_error_occurred, &error_message_detail, &session_id] // session_id 참조 캡처
            (const std::vector<uint8_t>& audio_chunk, const std::vector<avatar_sync::VisemeData>& visemes) {
            if (synthesis_error_occurred.load()) return;
            const std::string& current_session_id_ref = session_id; // 콜백 시점의 세션 ID 사용

            if (!audio_chunk.empty()) {
                if (!avatar_sync_client_->SendAudioChunk(audio_chunk)) {
                    std::cerr << "  ❌ [" << current_session_id_ref << "] Failed to send audio chunk to AvatarSync." << std::endl;
                    if(!synthesis_error_occurred.load()) error_message_detail = "AvatarSync SendAudioChunk failed.";
                    synthesis_error_occurred.store(true);
                }
            }
            if (!visemes.empty()) {
                if (!avatar_sync_client_->SendVisemeDataBatch(visemes)) {
                     std::cerr << "  ❌ [" << current_session_id_ref << "] Failed to send viseme data to AvatarSync." << std::endl;
                     if(!synthesis_error_occurred.load()) error_message_detail = "AvatarSync SendVisemeDataBatch failed.";
                     synthesis_error_occurred.store(true);
                }
            }
        };

        while (reader->Read(&request)) {
            if (context->IsCancelled()) {
                error_message_detail = "Request cancelled by LLM client.";
                std::cerr << "🚫 [" << (session_id.empty() ? client_peer : session_id) << "] " << error_message_detail << std::endl;
                synthesis_error_occurred.store(true);
                break;
            }

            if (request.has_config()) {
                // ... (기존 config 처리 로직, 수정 없음) ...
                 const auto& config = request.config();
                 if (session_id.empty()) { // 첫 번째 config 메시지
                     session_id = config.session_id();
                     if (session_id.empty()) {
                         session_id = generate_uuid();
                         std::cout << "⚠️ LLM did not provide session_id. Generated: " << session_id << std::endl;
                     }
                     active_synthesis_config = config;
                     active_synthesis_config.set_session_id(session_id);

                     std::cout << "▶️ [" << session_id << "] Received SynthesisConfig: Lang=" << config.language_code()
                               << ", Voice=" << config.voice_name() << std::endl;

                     tts_engine = tts_engine_factory_();
                     if (!tts_engine || !tts_engine->InitializeSynthesis(active_synthesis_config)) {
                         error_message_detail = "Failed to initialize TTS engine with provided config.";
                         std::cerr << "❌ [" << session_id << "] " << error_message_detail << std::endl;
                         synthesis_error_occurred.store(true);
                         break;
                     }
                     tts_engine_initialized = true;
                     std::cout << "   [" << session_id << "] TTS engine initialized." << std::endl;

                     if (!avatar_sync_client_->StartStream(session_id)) {
                         error_message_detail = "Failed to start stream to AvatarSync service.";
                         std::cerr << "❌ [" << session_id << "] " << error_message_detail << std::endl;
                         synthesis_error_occurred.store(true);
                         break;
                     }
                     avatar_sync_stream_started = true;
                     std::cout << "   [" << session_id << "] Stream to AvatarSync started." << std::endl;

                 } else if (session_id != config.session_id()) {
                     error_message_detail = "Received new SynthesisConfig with a different session_id mid-stream. Not supported.";
                     std::cerr << "❌ [" << session_id << "] " << error_message_detail << " (New: " << config.session_id() << ")" << std::endl;
                     synthesis_error_occurred.store(true);
                     break;
                 } else {
                     std::cout << "🔄 [" << session_id << "] Received subsequent SynthesisConfig. Re-initializing TTS Engine." << std::endl;
                     if (!tts_engine || !tts_engine->InitializeSynthesis(config)) {
                          error_message_detail = "Failed to re-initialize TTS engine with new config.";
                          std::cerr << "❌ [" << session_id << "] " << error_message_detail << std::endl;
                          synthesis_error_occurred.store(true);
                          break;
                     }
                 }
                 first_message = false;

            } else if (request.request_data_case() == TTSStreamRequest::kTextChunk) {
                const std::string& text = request.text_chunk();

                // Config 누락 시 INVALID_ARGUMENT 반환 (오류 코드 수정)
                if (first_message || !tts_engine_initialized || !avatar_sync_stream_started) {
                    error_message_detail = "Received text_chunk before SynthesisConfig or before systems are initialized.";
                    std::cerr << "❌ [" << (session_id.empty() ? client_peer : session_id) << "] " << error_message_detail << std::endl;
                    cleanup_resources(session_id); // 리소스 정리 후 반환
                    return Status(StatusCode::INVALID_ARGUMENT, error_message_detail); // INVALID_ARGUMENT 반환
                }

                if (text.empty()) {
                    std::cout << "  [" << session_id << "] Received empty text chunk, skipping." << std::endl;
                    continue;
                }
                std::cout << "  [" << session_id << "] Received text chunk: \"" << text.substr(0, 30) << (text.length() > 30 ? "..." : "") << "\"" << std::endl;

                // 각 청크의 합성이 완료될 때까지 대기하기 위한 Promise/Future
                std::promise<bool> chunk_synthesis_promise;
                auto chunk_synthesis_future = chunk_synthesis_promise.get_future();
                bool synthesize_call_ok = false;

                // 현재 청크의 완료 시 chunk_synthesis_promise를 설정하는 콜백 람다
                auto chunk_completion_cb =
                    [&chunk_synthesis_promise, &synthesis_error_occurred, &error_message_detail, session_id_ref = session_id]
                    (bool success, const std::string& azure_msg) mutable { // mutable 키워드 추가 (promise 이동 또는 복사 가능성 대비)
                    std::cout << "ℹ️ [" << session_id_ref << "] TTS Engine Synthesize (chunk) completed. Success: " << success;
                    if (!azure_msg.empty()) std::cout << ", Msg: " << azure_msg;
                    std::cout << std::endl;

                    if (!success) {
                        if(!synthesis_error_occurred.load()) { // 오류가 처음 발생한 경우만 메시지 저장
                           error_message_detail = "TTS engine synthesis for chunk failed: " + azure_msg;
                        }
                        synthesis_error_occurred.store(true); // 오류 발생 플래그 설정
                    }
                    // set_value는 한 번만 호출되어야 함
                    try {
                        chunk_synthesis_promise.set_value(success);
                    } catch (const std::future_error& e) {
                        // 이미 설정되었거나 promise가 파괴된 경우 예외 발생 가능 (무시)
                         std::cerr << "  [" << session_id_ref << "] Warning: Exception setting chunk promise value: " << e.what() << std::endl;
                    }
                };

                // TTS 엔진 호출
                std::cout << "  [" << session_id << "] Calling TTS Engine Synthesize for current chunk..." << std::endl;
                synthesize_call_ok = tts_engine->Synthesize(text, audio_viseme_cb, chunk_completion_cb);

                if (!synthesize_call_ok) {
                    // Synthesize 호출 자체가 실패한 경우 (예: 엔진 비활성 등 동기적 오류)
                    // 이 경우 error_message_detail은 AzureTTSEngine 내부에서 설정됨
                    error_message_detail = "TTS Engine Synthesize call failed immediately (returned false). Check engine logs.";
                    std::cerr << "❌ [" << session_id << "] " << error_message_detail << std::endl;
                    synthesis_error_occurred.store(true);
                    // 이 경우 chunk_completion_cb가 호출되지 않았을 수 있으므로, promise를 설정하여 대기를 해제
                    try { chunk_synthesis_promise.set_value(false); } catch (const std::future_error&) {}
                    break; // 루프 탈출
                } else {
                    // Synthesize 호출 성공 (비동기 작업 시작됨), 완료 대기
                    std::cout << "  [" << session_id << "] Waiting for current chunk synthesis completion..." << std::endl;
                    // 타임아웃 설정 (개별 청크 처리 시간 고려)
                    std::chrono::seconds chunk_timeout(25);
                    std::future_status status = chunk_synthesis_future.wait_for(chunk_timeout);

                    if (status == std::future_status::timeout) {
                        error_message_detail = "Timeout waiting for current text chunk synthesis to complete (" + std::to_string(chunk_timeout.count()) + "s).";
                        std::cerr << "❌ [" << session_id << "] " << error_message_detail << std::endl;
                        synthesis_error_occurred.store(true);
                        if (tts_engine) tts_engine->StopSynthesis(); // 진행 중인 합성 중단 시도
                        break; // 루프 탈출
                    }

                    // 완료됨, 결과 확인 (오류 발생 시 콜백에서 synthesis_error_occurred가 true로 설정됨)
                    bool chunk_success = chunk_synthesis_future.get(); // promise에서 설정된 값 가져오기
                    if (!chunk_success) {
                        // 콜백에서 오류가 보고됨 (synthesis_error_occurred 는 이미 true)
                        std::cerr << "❌ [" << session_id << "] Current text chunk synthesis reported failure." << std::endl;
                        // error_message_detail은 콜백에서 설정되었을 것임
                        break; // 루프 탈출
                    }
                    // 성공 시 다음 루프 반복으로 진행
                     std::cout << "  [" << session_id << "] Current chunk synthesis completed successfully." << std::endl;
                }
            } else { // 빈 요청 처리 등
                error_message_detail = "Received TTSStreamRequest with no data set.";
                std::cerr << "❌ [" << (session_id.empty() ? client_peer : session_id) << "] " << error_message_detail << std::endl;
                synthesis_error_occurred.store(true);
                break;
            }
        } // End while reader->Read()

        // --- 루프 종료 후 처리 ---
        if (synthesis_error_occurred.load()) {
             std::cerr << "⏪ [" << session_id << "] Exiting processing loop due to error or client cancellation." << std::endl;
             // 오류 발생 시 전체 스트림 완료 promise 설정하여 즉시 정리 단계로 이동
             try { overall_synthesis_complete_promise.set_value(); } catch (const std::future_error&) {}
        } else {
            std::cout << "ℹ️ [" << (session_id.empty() ? client_peer : session_id) << "] LLM client finished sending text chunks (stream closed)." << std::endl;
             // 클라이언트가 정상적으로 스트림을 닫으면, 모든 작업이 완료되었음을 알림
             try { overall_synthesis_complete_promise.set_value(); } catch (const std::future_error&) {}
        }

        // --- 전체 스트림 완료 대기 (오류 또는 정상 종료 신호) ---
        // 위 로직에서 오류 발생 시 또는 정상 종료 시 overall_synthesis_complete_promise가 설정됨
        std::cout << "   [" << session_id << "] Waiting for overall stream completion signal..." << std::endl;
        if (overall_synthesis_complete_future.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
             // 정상/오류 경로에서 promise가 설정되지 않은 예외적 상황 (로직 오류 가능성)
             std::cerr << "   ⚠️ [" << session_id << "] Timeout waiting for overall stream completion signal (5s). Logic might be flawed." << std::endl;
             if (!synthesis_error_occurred.load()) { // 이전에 오류가 없었다면 타임아웃을 오류로 간주
                error_message_detail = "Internal timeout waiting for overall completion signal.";
                synthesis_error_occurred.store(true);
             }
        } else {
             std::cout << "   [" << session_id << "] Overall stream completion signaled." << std::endl;
        }

    } catch (const std::exception& e) {
        error_message_detail = "Unhandled C++ exception in SynthesizeStream: " + std::string(e.what());
        std::cerr << "❌ [" << (session_id.empty() ? client_peer : session_id) << "] " << error_message_detail << std::endl;
        synthesis_error_occurred.store(true);
        try { overall_synthesis_complete_promise.set_value(); } catch (const std::future_error&) {}
    } catch (...) {
        error_message_detail = "Unknown C++ exception in SynthesizeStream.";
        std::cerr << "❌ [" << (session_id.empty() ? client_peer : session_id) << "] " << error_message_detail << std::endl;
        synthesis_error_occurred.store(true);
        try { overall_synthesis_complete_promise.set_value(); } catch (const std::future_error&) {}
    }

    // --- 리소스 정리 ---
    cleanup_resources(session_id);

    // --- 최종 상태 반환 ---
    if (synthesis_error_occurred.load()) {
        std::cerr << "❌ [" << (session_id.empty() ? client_peer : session_id) << "] Finalizing with error: " << error_message_detail << std::endl;
        if (context->IsCancelled()) {
            return Status(StatusCode::CANCELLED, "Request cancelled by LLM client: " + error_message_detail);
        }
        // Config 누락 오류는 위에서 INVALID_ARGUMENT로 처리됨
        // 다른 내부 오류들은 INTERNAL 반환
        return Status(StatusCode::INTERNAL, "TTS stream processing failed: " + error_message_detail);
    }

    std::cout << "✅ [" << (session_id.empty() ? client_peer : session_id) << "] TTS Stream processing completed successfully." << std::endl;
    return Status::OK;
}

} // namespace tts