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

// avatar_sync.proto에서 생성된 헤더 (avatar_sync::SyncConfig 사용 위함)
// 이 헤더는 tts_service.h 또는 avatar_sync_client.h를 통해 이미 포함될 수 있습니다.
// 명시적으로 추가하는 것이 안전할 수 있습니다.
#include "avatar_sync.pb.h"


using grpc::Status;
using grpc::StatusCode;

namespace tts {

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
    std::string tts_internal_session_id; // TTS 서비스 내부에서 사용하는 세션 ID
    std::string frontend_session_id;     // 프론트엔드 웹소켓 세션 ID

    std::cout << "✅ New LLM client connection for TTS from: " << client_peer
              << " (Thread ID: " << std::this_thread::get_id() << ")" << std::endl;

    std::unique_ptr<AzureTTSEngine> tts_engine = nullptr;
    SynthesisConfig active_synthesis_config; // 현재 활성화된 TTS 설정 (frontend_session_id 포함)
    bool tts_engine_initialized = false;
    bool avatar_sync_stream_started = false;
    std::atomic<bool> synthesis_error_occurred{false};
    std::string error_message_detail;

    std::promise<void> overall_synthesis_complete_promise;
    auto overall_synthesis_complete_future = overall_synthesis_complete_promise.get_future();

    // Cleanup lambda: 로그에 사용할 세션 ID를 명확히 하기 위해 fe_session_id_ref 추가
    auto cleanup_resources = [&](const std::string& tts_session_id_ref, const std::string& fe_session_id_ref) {
         std::cout << "🧹 TTS_Service [TTS_SID:" << (tts_session_id_ref.empty() ? "NO_TTS_SID" : tts_session_id_ref)
                   << ", FE_SID:" << (fe_session_id_ref.empty() ? "NO_FE_SID" : fe_session_id_ref)
                   << "] Cleaning up TTS resources..." << std::endl;

         if (avatar_sync_stream_started && avatar_sync_client_ && avatar_sync_client_->IsStreamActive()) {
             std::cout << "   Finishing AvatarSync stream for FE_SID [" << fe_session_id_ref << "]..." << std::endl;
             Status avatar_finish_status = avatar_sync_client_->FinishStream(); // FinishStream은 내부적으로 세션 ID를 알고 있음
             if (!avatar_finish_status.ok()) {
                 std::cerr << "   ⚠️ AvatarSync stream finish error during cleanup: ("
                           << avatar_finish_status.error_code() << ") "
                           << avatar_finish_status.error_message() << std::endl;
             } else {
                 std::cout << "   AvatarSync stream finished successfully during cleanup for FE_SID [" << fe_session_id_ref << "]." << std::endl;
             }
         }
         avatar_sync_stream_started = false;

         if (tts_engine_initialized && tts_engine) {
             std::cout << "   Stopping/Finalizing TTS engine for TTS_SID [" << tts_session_id_ref << "]..." << std::endl;
             tts_engine->StopSynthesis();
         }
         tts_engine_initialized = false;
         tts_engine.reset();
    };

    try {
        TTSStreamRequest request;
        bool first_message = true;

        // Audio/Viseme 콜백: 로그에 tts_internal_session_id와 frontend_session_id를 모두 사용
        auto audio_viseme_cb =
            [this, &synthesis_error_occurred, &error_message_detail, &tts_internal_session_id, &frontend_session_id]
            (const std::vector<uint8_t>& audio_chunk, const std::vector<avatar_sync::VisemeData>& visemes) {
            if (synthesis_error_occurred.load()) return;

            if (!audio_chunk.empty()) {
                // std::cout << "  TTS_Service [TTS_SID:" << tts_internal_session_id << ", FE_SID:" << frontend_session_id << "] Sending audio chunk (" << audio_chunk.size() << " bytes) to AvatarSync." << std::endl;
                if (!avatar_sync_client_->SendAudioChunk(audio_chunk)) { // AvatarSyncClient는 내부적으로 frontend_session_id를 알고 있음
                    std::cerr << "  ❌ TTS_Service [TTS_SID:" << tts_internal_session_id << ", FE_SID:" << frontend_session_id << "] Failed to send audio chunk to AvatarSync." << std::endl;
                    if(!synthesis_error_occurred.load()) error_message_detail = "AvatarSync SendAudioChunk failed.";
                    synthesis_error_occurred.store(true);
                }
            }
            if (!visemes.empty()) {
                // std::cout << "  TTS_Service [TTS_SID:" << tts_internal_session_id << ", FE_SID:" << frontend_session_id << "] Sending " << visemes.size() << " visemes to AvatarSync." << std::endl;
                if (!avatar_sync_client_->SendVisemeDataBatch(visemes)) {
                     std::cerr << "  ❌ TTS_Service [TTS_SID:" << tts_internal_session_id << ", FE_SID:" << frontend_session_id << "] Failed to send viseme data to AvatarSync." << std::endl;
                     if(!synthesis_error_occurred.load()) error_message_detail = "AvatarSync SendVisemeDataBatch failed.";
                     synthesis_error_occurred.store(true);
                }
            }
        };

        while (reader->Read(&request)) {
            if (context->IsCancelled()) {
                error_message_detail = "Request cancelled by LLM client.";
                std::cerr << "🚫 TTS_Service [TTS_SID:" << (tts_internal_session_id.empty() ? client_peer : tts_internal_session_id)
                          << ", FE_SID:" << (frontend_session_id.empty() ? "N/A" : frontend_session_id)
                          << "] " << error_message_detail << std::endl;
                synthesis_error_occurred.store(true);
                break;
            }

            if (request.has_config()) {
                 const auto& received_config = request.config();
                 
                 // frontend_session_id는 첫 config에서만 설정하고, 이후 변경되지 않아야 함
                 if (first_message) {
                     frontend_session_id = received_config.frontend_session_id();
                     if (frontend_session_id.empty()) {
                         error_message_detail = "CRITICAL: frontend_session_id is missing in the initial SynthesisConfig from LLM.";
                         std::cerr << "❌ TTS_Service [Peer:" << client_peer << "] " << error_message_detail << std::endl;
                         synthesis_error_occurred.store(true);
                         // 이 경우, 클라이언트에게 오류를 알리고 스트림을 종료해야 함
                         cleanup_resources(tts_internal_session_id, frontend_session_id);
                         return Status(StatusCode::INVALID_ARGUMENT, error_message_detail);
                     }
                 } else if (frontend_session_id != received_config.frontend_session_id()) {
                     // 스트림 중간에 frontend_session_id가 변경되는 것은 허용하지 않음
                     error_message_detail = "CRITICAL: frontend_session_id changed mid-stream. This is not supported.";
                     std::cerr << "❌ TTS_Service [TTS_SID:" << tts_internal_session_id 
                               << ", Old_FE_SID:" << frontend_session_id 
                               << ", New_FE_SID:" << received_config.frontend_session_id() 
                               << "] " << error_message_detail << std::endl;
                     synthesis_error_occurred.store(true);
                     break; 
                 }

                 // TTS 내부 세션 ID 처리
                 if (tts_internal_session_id.empty()) { // 첫 번째 config 메시지
                     tts_internal_session_id = received_config.session_id(); // LLM이 제공한 ID 사용
                     if (tts_internal_session_id.empty()) { // LLM도 안줬으면 생성
                         tts_internal_session_id = generate_uuid();
                         std::cout << "⚠️ TTS_Service: LLM did not provide session_id. Generated TTS_SID: " << tts_internal_session_id 
                                   << " for FE_SID: " << frontend_session_id << std::endl;
                     }
                 } else if (tts_internal_session_id != received_config.session_id() && !received_config.session_id().empty()) {
                     // TTS 내부 세션 ID가 변경되었으면 경고 (보통은 변경되지 않아야 함)
                     std::cout << "🔄 TTS_Service [Old_TTS_SID:" << tts_internal_session_id 
                               << ", New_TTS_SID:" << received_config.session_id()
                               << ", FE_SID:" << frontend_session_id 
                               << "] Received subsequent SynthesisConfig with a different internal session_id. Updating." << std::endl;
                     tts_internal_session_id = received_config.session_id();
                 }
                 
                 active_synthesis_config.Clear(); // 이전 설정 초기화
                 active_synthesis_config.CopyFrom(received_config); // 새 설정 복사
                 active_synthesis_config.set_session_id(tts_internal_session_id); // 내부용 ID 확실히 설정
                 active_synthesis_config.set_frontend_session_id(frontend_session_id); // frontend_session_id도 확실히 설정

                 std::cout << "▶️ TTS_Service [TTS_SID:" << tts_internal_session_id 
                           << ", FE_SID:" << frontend_session_id 
                           << "] Received SynthesisConfig: Lang=" << active_synthesis_config.language_code()
                           << ", Voice=" << active_synthesis_config.voice_name() << std::endl;

                 tts_engine = tts_engine_factory_();
                 if (!tts_engine || !tts_engine->InitializeSynthesis(active_synthesis_config)) {
                     error_message_detail = "Failed to initialize TTS engine with provided config.";
                     std::cerr << "❌ TTS_Service [TTS_SID:" << tts_internal_session_id << ", FE_SID:" << frontend_session_id << "] " << error_message_detail << std::endl;
                     synthesis_error_occurred.store(true);
                     break;
                 }
                 tts_engine_initialized = true;
                 std::cout << "   TTS_Service [TTS_SID:" << tts_internal_session_id << "] TTS engine initialized." << std::endl;

                 if (first_message) { // AvatarSync 스트림은 첫 config 메시지 수신 시에만 시작
                     avatar_sync::SyncConfig avatar_config_to_send;
                     avatar_config_to_send.set_frontend_session_id(frontend_session_id); // ★ AvatarSync에는 frontend_session_id만 전달

                     std::cout << "   TTS_Service [TTS_SID:" << tts_internal_session_id << "] Starting stream to AvatarSync for FE_SID [" << frontend_session_id << "]..." << std::endl;
                     if (!avatar_sync_client_->StartStream(avatar_config_to_send)) { // AvatarSyncClient::StartStream은 SyncConfig 객체를 받음
                         error_message_detail = "Failed to start stream to AvatarSync service.";
                         std::cerr << "❌ TTS_Service [TTS_SID:" << tts_internal_session_id << ", FE_SID:" << frontend_session_id << "] " << error_message_detail << std::endl;
                         synthesis_error_occurred.store(true);
                         break;
                     }
                     avatar_sync_stream_started = true;
                     std::cout << "   TTS_Service [TTS_SID:" << tts_internal_session_id << "] Stream to AvatarSync started for FE_SID [" << frontend_session_id << "]." << std::endl;
                 }
                 first_message = false;

            } else if (request.request_data_case() == TTSStreamRequest::kTextChunk) {
                const std::string& text = request.text_chunk();

                if (first_message || !tts_engine_initialized || !avatar_sync_stream_started) {
                    error_message_detail = "Received text_chunk before SynthesisConfig or before dependent systems are initialized.";
                    std::cerr << "❌ TTS_Service [TTS_SID:" << (tts_internal_session_id.empty() ? client_peer : tts_internal_session_id)
                              << ", FE_SID:" << (frontend_session_id.empty() ? "N/A" : frontend_session_id)
                              << "] " << error_message_detail << std::endl;
                    cleanup_resources(tts_internal_session_id, frontend_session_id);
                    return Status(StatusCode::INVALID_ARGUMENT, error_message_detail);
                }

                if (text.empty()) {
                    std::cout << "  TTS_Service [TTS_SID:" << tts_internal_session_id << "] Received empty text chunk, skipping." << std::endl;
                    continue;
                }
                std::cout << "  TTS_Service [TTS_SID:" << tts_internal_session_id << ", FE_SID:" << frontend_session_id 
                          << "] Received text chunk: \"" << text.substr(0, 30) << (text.length() > 30 ? "..." : "") << "\"" << std::endl;

                std::promise<bool> chunk_synthesis_promise;
                auto chunk_synthesis_future = chunk_synthesis_promise.get_future();
                bool synthesize_call_ok = false;

                auto chunk_completion_cb =
                    [&chunk_synthesis_promise, &synthesis_error_occurred, &error_message_detail, tts_sid = tts_internal_session_id, fe_sid = frontend_session_id]
                    (bool success, const std::string& azure_msg) mutable {
                    std::cout << "ℹ️ TTS_Service [TTS_SID:" << tts_sid << ", FE_SID:" << fe_sid 
                              << "] TTS Engine Synthesize (chunk) completed. Success: " << success;
                    if (!azure_msg.empty()) std::cout << ", Msg: " << azure_msg;
                    std::cout << std::endl;

                    if (!success) {
                        if(!synthesis_error_occurred.load()) {
                           error_message_detail = "TTS engine synthesis for chunk failed: " + azure_msg;
                        }
                        synthesis_error_occurred.store(true);
                    }
                    try {
                        chunk_synthesis_promise.set_value(success);
                    } catch (const std::future_error& e) {
                         std::cerr << "  TTS_Service [TTS_SID:" << tts_sid << "] Warning: Exception setting chunk promise value: " << e.what() << std::endl;
                    }
                };

                std::cout << "  TTS_Service [TTS_SID:" << tts_internal_session_id << "] Calling TTS Engine Synthesize for current chunk..." << std::endl;
                synthesize_call_ok = tts_engine->Synthesize(text, audio_viseme_cb, chunk_completion_cb);

                if (!synthesize_call_ok) {
                    error_message_detail = "TTS Engine Synthesize call failed immediately. Check engine logs.";
                    std::cerr << "❌ TTS_Service [TTS_SID:" << tts_internal_session_id << ", FE_SID:" << frontend_session_id << "] " << error_message_detail << std::endl;
                    synthesis_error_occurred.store(true);
                    try { chunk_synthesis_promise.set_value(false); } catch (const std::future_error&) {}
                    break;
                } else {
                    std::cout << "  TTS_Service [TTS_SID:" << tts_internal_session_id << "] Waiting for current chunk synthesis completion..." << std::endl;
                    std::chrono::seconds chunk_timeout(25); // 타임아웃 증가 또는 설정값으로 변경
                    std::future_status status = chunk_synthesis_future.wait_for(chunk_timeout);

                    if (status == std::future_status::timeout) {
                        error_message_detail = "Timeout waiting for current text chunk synthesis to complete (" + std::to_string(chunk_timeout.count()) + "s).";
                        std::cerr << "❌ TTS_Service [TTS_SID:" << tts_internal_session_id << ", FE_SID:" << frontend_session_id << "] " << error_message_detail << std::endl;
                        synthesis_error_occurred.store(true);
                        if (tts_engine) tts_engine->StopSynthesis();
                        break;
                    }
                    bool chunk_success = chunk_synthesis_future.get();
                    if (!chunk_success) {
                        std::cerr << "❌ TTS_Service [TTS_SID:" << tts_internal_session_id << ", FE_SID:" << frontend_session_id << "] Current text chunk synthesis reported failure." << std::endl;
                        break;
                    }
                     std::cout << "  TTS_Service [TTS_SID:" << tts_internal_session_id << "] Current chunk synthesis completed successfully." << std::endl;
                }
            } else {
                error_message_detail = "Received TTSStreamRequest with no data set.";
                std::cerr << "❌ TTS_Service [TTS_SID:" << (tts_internal_session_id.empty() ? client_peer : tts_internal_session_id)
                          << ", FE_SID:" << (frontend_session_id.empty() ? "N/A" : frontend_session_id)
                          << "] " << error_message_detail << std::endl;
                synthesis_error_occurred.store(true);
                break;
            }
        } // End while reader->Read()

        if (synthesis_error_occurred.load()) {
             std::cerr << "⏪ TTS_Service [TTS_SID:" << tts_internal_session_id << ", FE_SID:" << frontend_session_id << "] Exiting processing loop due to error or client cancellation." << std::endl;
             try { overall_synthesis_complete_promise.set_value(); } catch (const std::future_error&) {}
        } else {
            std::cout << "ℹ️ TTS_Service [TTS_SID:" << (tts_internal_session_id.empty() ? client_peer : tts_internal_session_id)
                      << ", FE_SID:" << (frontend_session_id.empty() ? "N/A" : frontend_session_id)
                      << "] LLM client finished sending text chunks (stream closed)." << std::endl;
             try { overall_synthesis_complete_promise.set_value(); } catch (const std::future_error&) {}
        }

        std::cout << "   TTS_Service [TTS_SID:" << tts_internal_session_id << "] Waiting for overall stream completion signal..." << std::endl;
        if (overall_synthesis_complete_future.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
             std::cerr << "   ⚠️ TTS_Service [TTS_SID:" << tts_internal_session_id << "] Timeout waiting for overall stream completion signal (5s). Logic might be flawed." << std::endl;
             if (!synthesis_error_occurred.load()) {
                error_message_detail = "Internal timeout waiting for overall completion signal.";
                synthesis_error_occurred.store(true);
             }
        } else {
             std::cout << "   TTS_Service [TTS_SID:" << tts_internal_session_id << "] Overall stream completion signaled." << std::endl;
        }

    } catch (const std::exception& e) {
        error_message_detail = "Unhandled C++ exception in SynthesizeStream: " + std::string(e.what());
        std::cerr << "❌ TTS_Service [TTS_SID:" << (tts_internal_session_id.empty() ? client_peer : tts_internal_session_id)
                  << ", FE_SID:" << (frontend_session_id.empty() ? "N/A" : frontend_session_id)
                  << "] " << error_message_detail << std::endl;
        synthesis_error_occurred.store(true);
        try { overall_synthesis_complete_promise.set_value(); } catch (const std::future_error&) {}
    } catch (...) {
        error_message_detail = "Unknown C++ exception in SynthesizeStream.";
        std::cerr << "❌ TTS_Service [TTS_SID:" << (tts_internal_session_id.empty() ? client_peer : tts_internal_session_id)
                  << ", FE_SID:" << (frontend_session_id.empty() ? "N/A" : frontend_session_id)
                  << "] " << error_message_detail << std::endl;
        synthesis_error_occurred.store(true);
        try { overall_synthesis_complete_promise.set_value(); } catch (const std::future_error&) {}
    }

    cleanup_resources(tts_internal_session_id, frontend_session_id);

    if (synthesis_error_occurred.load()) {
        std::cerr << "❌ TTS_Service [TTS_SID:" << (tts_internal_session_id.empty() ? client_peer : tts_internal_session_id)
                  << ", FE_SID:" << (frontend_session_id.empty() ? "N/A" : frontend_session_id)
                  << "] Finalizing with error: " << error_message_detail << std::endl;
        if (context->IsCancelled()) {
            return Status(StatusCode::CANCELLED, "Request cancelled by LLM client: " + error_message_detail);
        }
        return Status(StatusCode::INTERNAL, "TTS stream processing failed: " + error_message_detail);
    }

    std::cout << "✅ TTS_Service [TTS_SID:" << (tts_internal_session_id.empty() ? client_peer : tts_internal_session_id)
              << ", FE_SID:" << (frontend_session_id.empty() ? "N/A" : frontend_session_id)
              << "] TTS Stream processing completed successfully." << std::endl;
    return Status::OK;
}

} // namespace tts
