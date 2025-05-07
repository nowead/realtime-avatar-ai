#include "tts_service.h"
#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>   // For logging timestamps, durations
#include <thread>   // For this_thread::get_id, sleep_for
#include <future>   // For std::promise, std::future

// grpc 상태 코드 사용 편의성
using grpc::Status;
using grpc::StatusCode;

namespace tts {

// 간단한 UUID 생성 함수 (이전과 동일)
std::string TTSServiceImpl::generate_uuid() {
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
    std::string session_id; // LLM이 제공하는 세션 ID
    std::cout << "✅ New LLM client connection for TTS from: " << client_peer
              << " (Thread ID: " << std::this_thread::get_id() << ")" << std::endl;

    std::unique_ptr<AzureTTSEngine> tts_engine = nullptr;
    SynthesisConfig active_synthesis_config;
    bool tts_engine_initialized = false;
    bool avatar_sync_stream_started = false;
    std::atomic<bool> synthesis_error_occurred{false}; // TTS 엔진 내부 오류 또는 콜백 오류 플래그
    std::string error_message_detail; // 상세 오류 메시지

    std::promise<void> synthesis_complete_promise; // TTS 엔진의 모든 합성이 완료되었음을 알리는 promise
    auto synthesis_complete_future = synthesis_complete_promise.get_future();

    // --- Graceful cleanup lambda ---
    auto cleanup_resources = [&](const std::string& current_session_id_ref) {
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
        avatar_sync_stream_started = false; // 상태 업데이트

        if (tts_engine_initialized && tts_engine) {
            std::cout << "   Stopping/Finalizing TTS engine for session [" << current_session_id_ref << "]..." << std::endl;
            tts_engine->StopSynthesis(); // 진행 중인 합성 중단 요청
            // AzureTTSEngine 소멸자에서 나머지 정리
        }
        tts_engine_initialized = false; // 상태 업데이트
        tts_engine.reset(); // 엔진 객체 해제
    };


    try {
        TTSStreamRequest request;
        bool first_message = true;
        std::string accumulated_text_for_synthesis; // 텍스트 모아서 처리 (선택적)

        while (reader->Read(&request)) {
            if (context->IsCancelled()) {
                error_message_detail = "Request cancelled by LLM client.";
                std::cerr << "🚫 [" << (session_id.empty() ? client_peer : session_id) << "] " << error_message_detail << std::endl;
                synthesis_error_occurred.store(true);
                break; // 루프 탈출
            }

            if (request.has_config()) {
                const auto& config = request.config();
                if (session_id.empty()) { // 첫 번째 config 메시지
                    session_id = config.session_id();
                    if (session_id.empty()) {
                        session_id = generate_uuid(); // LLM이 안주면 생성
                        std::cout << "⚠️ LLM did not provide session_id. Generated: " << session_id << std::endl;
                    }
                    active_synthesis_config = config;
                    active_synthesis_config.set_session_id(session_id); // 세션 ID 보장

                    std::cout << "▶️ [" << session_id << "] Received SynthesisConfig: Lang=" << config.language_code()
                              << ", Voice=" << config.voice_name() << std::endl;

                    // 1. TTS 엔진 생성 및 초기화
                    tts_engine = tts_engine_factory_(); // 팩토리로부터 엔진 인스턴스 받기
                    if (!tts_engine || !tts_engine->InitializeSynthesis(active_synthesis_config)) {
                        error_message_detail = "Failed to initialize TTS engine with provided config.";
                        std::cerr << "❌ [" << session_id << "] " << error_message_detail << std::endl;
                        synthesis_error_occurred.store(true);
                        break;
                    }
                    tts_engine_initialized = true;
                    std::cout << "   [" << session_id << "] TTS engine initialized." << std::endl;

                    // 2. AvatarSync 서비스로 스트림 시작
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

            } else if (request.request_data_case() == TTSStreamRequest::kTextChunk) { // 또는 request.has_text_chunk()가 맞다면 .proto 파일 확인
                const std::string& text = request.text_chunk();
                if (first_message || !tts_engine_initialized || !avatar_sync_stream_started) {
                    error_message_detail = "Received text_chunk before SynthesisConfig or before systems are initialized.";
                    std::cerr << "❌ [" << (session_id.empty() ? client_peer : session_id) << "] " << error_message_detail << std::endl;
                    synthesis_error_occurred.store(true);
                    break;
                }
                if (text.empty()) {
                    std::cout << "  [" << session_id << "] Received empty text chunk, skipping." << std::endl;
                    continue;
                }
                std::cout << "  [" << session_id << "] Received text chunk: \"" << text.substr(0, 30) << (text.length() > 30 ? "..." : "") << "\"" << std::endl;

                accumulated_text_for_synthesis += text + " "; // 간단히 텍스트 축적 (문장 단위 처리를 위해 필요시 수정)

                // Azure TTS는 일반적으로 긴 텍스트를 한 번에 처리하는 것이 효율적일 수 있음.
                // 또는 특정 구분자(마침표 등)가 오면 축적된 텍스트를 합성 요청할 수 있음.
                // 여기서는 간단히 받은 텍스트를 바로 합성 요청.
                // 더 나은 방법: 문장 단위로 끊어서 TTS 엔진에 전달.
                // 지금은 받은 청크를 바로바로 보낸다고 가정. (AzureTTSEngine의 Synthesize는 비동기)
                if (!accumulated_text_for_synthesis.empty()) { // 또는 특정 조건 만족 시
                     std::cout << "  [" << session_id << "] Calling TTS Engine Synthesize for: \""
                               << accumulated_text_for_synthesis.substr(0, 50) << "...\"" << std::endl;

                    // TTS 엔진 콜백 정의
                    auto audio_viseme_cb =
                        [this, current_session_id_ref = session_id, &synthesis_error_occurred, &error_message_detail]
                        (const std::vector<uint8_t>& audio_chunk, const std::vector<avatar_sync::VisemeData>& visemes) {
                        if (synthesis_error_occurred.load()) return; // 이미 오류 발생 시 콜백 처리 중단

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

                    auto synthesis_completion_cb =
                        [this, current_session_id_ref = session_id, &synthesis_error_occurred, &error_message_detail, &synthesis_complete_promise]
                        (bool success, const std::string& azure_msg) {
                        std::cout << "ℹ️ [" << current_session_id_ref << "] TTS Engine Synthesize completed. Success: " << success;
                        if (!azure_msg.empty()) std::cout << ", Msg: " << azure_msg;
                        std::cout << std::endl;

                        if (!success) {
                            if(!synthesis_error_occurred.load()) error_message_detail = "TTS engine synthesis failed: " + azure_msg;
                            synthesis_error_occurred.store(true);
                        }
                        // 개별 Synthesize 호출의 완료가 아닌, 전체 스트림의 완료를 알려야 함.
                        // 여기서는 LLM이 스트림을 닫을 때까지 계속 TTS를 호출할 수 있으므로,
                        // 이 콜백이 '모든' 작업의 끝을 의미하진 않음.
                        // LLM이 스트림을 닫으면 그때 synthesis_complete_promise.set_value()를 호출.
                        // 만약 오류 발생 시 즉시 promise를 set 하여 메인 루프가 대기에서 빠져나오도록 할 수 있음.
                        if (synthesis_error_occurred.load()) {
                            try { synthesis_complete_promise.set_value(); } catch (const std::future_error&) {}
                        }
                    };

                    if (!tts_engine->Synthesize(accumulated_text_for_synthesis, audio_viseme_cb, synthesis_completion_cb)) {
                        error_message_detail = "TTS Engine Synthesize call failed immediately.";
                        std::cerr << "❌ [" << session_id << "] " << error_message_detail << std::endl;
                        synthesis_error_occurred.store(true);
                        break; // 루프 탈출
                    }
                    accumulated_text_for_synthesis.clear(); // 처리된 텍스트 비우기
                }
            } else {
                error_message_detail = "Received TTSStreamRequest with no data set.";
                std::cerr << "❌ [" << (session_id.empty() ? client_peer : session_id) << "] " << error_message_detail << std::endl;
                synthesis_error_occurred.store(true);
                break;
            }
        } // End while reader->Read()

        // 루프 종료 후 처리
        if (synthesis_error_occurred.load()) {
            // 오류 발생 시 이미 로깅 및 error_message_detail 설정됨
             std::cerr << "⏪ [" << session_id << "] Exiting processing loop due to error or client cancellation." << std::endl;
        } else {
            // LLM 클라이언트가 스트림을 정상적으로 닫음
            std::cout << "ℹ️ [" << (session_id.empty() ? client_peer : session_id) << "] LLM client finished sending text chunks." << std::endl;
            // 만약 마지막으로 축적된 텍스트가 있다면 여기서 처리할 수 있지만, AzureTTSEngine이 비동기이므로
            // LLM 스트림 종료 시점에는 이미 진행중인 합성이 있을 수 있음.
            // AzureTTSEngine의 completion callback이 모두 완료될 때까지 기다려야 함.
        }

        // 모든 LLM 요청 처리가 끝났으므로, TTS 엔진의 최종 완료를 기다림.
        // 단, 오류가 이미 발생했거나 클라이언트가 취소했다면 즉시 정리로 넘어갈 수 있도록 promise를 설정.
        if (synthesis_error_occurred.load() || context->IsCancelled()) {
             try { synthesis_complete_promise.set_value(); } catch (const std::future_error&) {}
        } else {
            // 아직 처리 중인 TTS가 있을 수 있으므로, LLM이 스트림을 닫았다고 해서 바로 promise를 설정하지 않고,
            // 마지막 TTS의 completion_callback에서 promise를 설정하거나,
            // 또는 AzureTTSEngine에 '모든 작업 완료'를 알리는 메커니즘 필요.
            // 여기서는 단순화를 위해, LLM 스트림이 닫히면 더 이상 TTS 요청은 없으므로
            // 진행 중인 TTS가 완료되길 기다리는 형태로 가정.
            // AzureTTS Engine의 Synthesize 호출이 비동기이고, 내부적으로 완료 콜백을 호출하므로,
            // 모든 텍스트 청크에 대해 Synthesize 호출이 끝났다면,
            // 마지막 Synthesize의 completion_callback에서 set_value()를 호출하도록 설계하거나,
            // 또는 여기서 타임아웃을 두고 기다릴 수 있음.

            // 현재 구조에서는 LLM이 스트림을 닫으면 마지막 TTS 청크가 완료되길 기다려야 함.
            // 이 promise는 AzureTTSEngine의 *마지막* completion callback에서 set 되어야 이상적.
            // 임시로, 여기서는 LLM이 닫으면 TTS도 끝났다고 가정하고 promise 설정
            // (실제로는 비동기 처리로 인해 아직 TTS 작업 중일 수 있음)
            // -> 더 나은 방법: SynthesizeStream 종료 직전까지 TTS 완료 대기
             std::cout << "   [" << session_id << "] Waiting for any pending TTS synthesis to complete..." << std::endl;
             // 만약 에러가 없었고, 클라이언트가 취소하지 않았는데 LLM이 스트림을 닫은 경우.
             // 이 경우, 마지막 TTS 요청의 완료 콜백에서 promise를 설정해야 한다.
             // 이 예제에서는 단순화를 위해, 만약 아직 promise가 설정 안됐으면, 여기서 타임아웃을 두고 기다린다.
             if (synthesis_complete_future.wait_for(std::chrono::seconds(0)) == std::future_status::timeout) {
                 std::cout << "   [" << session_id << "] No explicit completion signal yet. Assuming TTS will finish or timeout." << std::endl;
                 // 여기서 강제로 set_value를 하면, 실제 TTS 완료 전에 넘어갈 수 있다.
                 // 실제 구현에서는 AzureTTSEngine 내부의 마지막 콜백에서 set_value를 호출해야 함.
                 // 지금은 Reader 루프가 끝났으므로, 모든 TTS 요청은 들어갔다고 가정하고,
                 // 진행중인 TTS의 완료를 기다린다.
             }
        }

        // TTS 엔진의 모든 작업이 완료되거나 타임아웃될 때까지 대기
        if (synthesis_complete_future.wait_for(std::chrono::seconds(30)) == std::future_status::timeout) {
            std::cerr << "   ⚠️ [" << session_id << "] Timeout waiting for all TTS engine tasks to complete (30s)." << std::endl;
            if (!synthesis_error_occurred.load()) {
                error_message_detail = "Timeout waiting for TTS completion.";
                synthesis_error_occurred.store(true);
            }
        } else {
            std::cout << "   [" << session_id << "] All TTS engine tasks believed to be complete or error signaled." << std::endl;
        }


    } catch (const std::exception& e) {
        error_message_detail = "Unhandled C++ exception in SynthesizeStream: " + std::string(e.what());
        std::cerr << "❌ [" << (session_id.empty() ? client_peer : session_id) << "] " << error_message_detail << std::endl;
        synthesis_error_occurred.store(true);
        try { synthesis_complete_promise.set_value(); } catch (const std::future_error&) {} // Ensure future unblocks
    } catch (...) {
        error_message_detail = "Unknown C++ exception in SynthesizeStream.";
        std::cerr << "❌ [" << (session_id.empty() ? client_peer : session_id) << "] " << error_message_detail << std::endl;
        synthesis_error_occurred.store(true);
        try { synthesis_complete_promise.set_value(); } catch (const std::future_error&) {} // Ensure future unblocks
    }

    // --- 리소스 정리 ---
    cleanup_resources(session_id);

    // --- 최종 상태 반환 ---
    if (synthesis_error_occurred.load()) {
        std::cerr << "❌ [" << (session_id.empty() ? client_peer : session_id) << "] Finalizing with error: " << error_message_detail << std::endl;
        if (context->IsCancelled()) {
            return Status(StatusCode::CANCELLED, "Request cancelled by LLM client: " + error_message_detail);
        }
        return Status(StatusCode::INTERNAL, "TTS stream processing failed: " + error_message_detail);
    }

    std::cout << "✅ [" << (session_id.empty() ? client_peer : session_id) << "] TTS Stream processing completed successfully." << std::endl;
    return Status::OK;
}

} // namespace tts