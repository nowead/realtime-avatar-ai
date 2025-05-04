// stt_service.cpp (수정 제안된 전체 코드)

#include "stt_service.h"
#include <iostream>
#include <string>
#include <vector>
#include <future>
#include <atomic>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread> // for sleep_for (optional)

// 필요한 경우 추가 헤더
#include "llm_engine_client.h" // LLMEngineClient 사용 위해 필요
#include "azure_stt_client.h" // AzureSTTClient 사용 위해 필요
#include <google/protobuf/empty.pb.h> // Empty 타입 사용 위해 필요
#include "stt.pb.h" // STTStreamRequest 등 사용 위해 필요

// grpc 상태 코드 사용 편의성
using grpc::Status;
using grpc::StatusCode;

namespace stt {

// 간단한 UUID 생성 함수 (변경 없음)
std::string STTServiceImpl::generate_uuid() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;

    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(16) << dis(gen);
    ss << std::setw(16) << dis(gen);
    return ss.str();
}

// 생성자 (변경 없음)
STTServiceImpl::STTServiceImpl(std::shared_ptr<AzureSTTClient> azure_client,
                               std::shared_ptr<LLMEngineClient> llm_client)
  : azure_stt_client_(azure_client), llm_engine_client_(llm_client)
{
    if (!azure_stt_client_) {
        throw std::runtime_error("AzureSTTClient cannot be null in STTServiceImpl.");
    }
    if (!llm_engine_client_) {
        throw std::runtime_error("LLMEngineClient cannot be null in STTServiceImpl.");
    }
}

// Client Streaming RPC 구현 (내부 로직 수정됨)
Status STTServiceImpl::RecognizeStream(
    ServerContext* context,
    ServerReader<STTStreamRequest>* reader,
    google::protobuf::Empty* response // 수정됨: 타입 명시 (using 대신)
) {
    const std::string client_peer = context->peer();
    const std::string session_id = generate_uuid();
    std::cout << "✅ [" << session_id << "] New client connection from: " << client_peer << std::endl;

    std::string language;
    std::atomic<bool> azure_started{false};
    std::atomic<bool> llm_stream_started{false};
    std::atomic<bool> stream_error_occurred{false};
    std::string error_message;

    std::promise<void> azure_processing_complete_promise;
    auto azure_processing_complete_future = azure_processing_complete_promise.get_future();

    // ---= Graceful cleanup lambda function (내부 수정됨) =---
    auto cleanup_resources = [&](bool stop_azure, bool finish_llm) {
         std::cout << "🧹 [" << session_id << "] Cleaning up resources... StopAzure=" << stop_azure << ", FinishLLM=" << finish_llm << std::endl;
         if (finish_llm && llm_engine_client_ && llm_stream_started.load()) {
             std::cout << "   Finishing LLM stream..." << std::endl;
             // 수정됨: Status 객체 하나만 받음
             grpc::Status llm_status = llm_engine_client_->FinishStream();
             if (!llm_status.ok()) {
                  std::cerr << "   ⚠️ LLM stream finish error during cleanup: (" << llm_status.error_code() << ") "
                            << llm_status.error_message() << std::endl;
             } else {
                  // 수정됨: llm_summary 관련 로깅 제거
                  std::cout << "   LLM stream finished successfully during cleanup." << std::endl;
             }
             llm_stream_started.store(false);
         }
         if (stop_azure && azure_stt_client_ && azure_started.load()) {
              std::cout << "   Stopping Azure recognition..." << std::endl;
              // 헤더 수정 후 이 함수 호출이 가능해야 함
              azure_stt_client_->StopContinuousRecognition();
              azure_started.store(false);
         }
    };

    try {
        // --- 1. 첫 메시지 (Config) 읽기 --- (변경 없음)
        STTStreamRequest initial_request;
        std::cout << "   [" << session_id << "] Waiting for initial RecognitionConfig..." << std::endl;
        if (!reader->Read(&initial_request)) {
            error_message = "Failed to read initial request from client.";
            std::cerr << "❌ [" << session_id << "] " << error_message << " Peer: " << client_peer << std::endl;
            return Status(StatusCode::INVALID_ARGUMENT, error_message);
        }
        // ... (config 읽기 및 유효성 검사 - 기존과 동일) ...
        if (initial_request.request_data_case() != STTStreamRequest::kConfig) {
            error_message = "Initial request must be RecognitionConfig.";
            std::cerr << "❌ [" << session_id << "] " << error_message << std::endl;
            return Status(StatusCode::INVALID_ARGUMENT, error_message);
        }
        language = initial_request.config().language();
        if (language.empty()) {
           error_message = "Language code is missing in RecognitionConfig.";
           std::cerr << "❌ [" << session_id << "] " << error_message << std::endl;
           return Status(StatusCode::INVALID_ARGUMENT, error_message);
        }
        std::cout << "   [" << session_id << "] Config received: Language=" << language << std::endl;


        // --- 2. LLM Engine 스트림 시작 --- (변경 없음)
        std::cout << "   [" << session_id << "] Starting stream to LLM Engine..." << std::endl;
        if (!llm_engine_client_->StartStream(session_id)) {
            error_message = "Failed to start stream to LLM Engine.";
            std::cerr << "❌ [" << session_id << "] " << error_message << std::endl;
            stream_error_occurred.store(true);
            return Status(StatusCode::INTERNAL, error_message);
        }
        llm_stream_started.store(true);
        std::cout << "   [" << session_id << "] LLM stream started successfully." << std::endl;


        // --- 3. Azure STT 콜백 정의 (내부 수정됨) ---

        // TextChunkCallback: Azure -> LLM 텍스트 전달
        auto text_callback =
            [this, session_id, &stream_error_occurred, &error_message, llm_client = llm_engine_client_]
            (const std::string& text, bool is_final) { // is_final 은 Azure 콜백에서 오지만 LLM 전송 시 사용 안함

            if (stream_error_occurred.load()) return;

            // 수정됨: is_final 인자 제거하고 호출
            if (!llm_client->SendTextChunk(text)) {
                std::cerr << "❌ [" << session_id << "] Error sending text chunk to LLM Engine. Marking stream as error." << std::endl;
                if (!stream_error_occurred.load()) {
                     error_message = "Failed to forward text chunk to LLM engine.";
                     stream_error_occurred.store(true);
                }
            }
        };

        // RecognitionCompletionCallback: Azure 인식 완료/오류 시 호출됨 (변경 없음)
        auto completion_callback =
            [this, session_id, &stream_error_occurred, &error_message, &azure_processing_complete_promise]
            (bool success, const std::string& azure_msg) {
            // ... (기존과 동일) ...
             std::cout << "ℹ️ [" << session_id << "] Azure STT processing finished. Success: " << success << std::endl;
             if (!success) {
                 std::cerr << "   Azure STT Error: " << azure_msg << std::endl;
                 if (!stream_error_occurred.load()) {
                     error_message = "Azure STT recognition failed: " + azure_msg;
                     stream_error_occurred.store(true);
                 }
             }
             try {
                 azure_processing_complete_promise.set_value();
             } catch (const std::future_error& e) {
                  std::cerr << "ℹ️ [" << session_id << "] Promise already set in completion_callback: " << e.what() << std::endl;
             }
        };


        // --- 4. Azure STT 인식 시작 --- (변경 없음)
        std::cout << "   [" << session_id << "] Starting Azure continuous recognition..." << std::endl;
        if (!azure_stt_client_->StartContinuousRecognition(language, text_callback, completion_callback)) {
            error_message = "Failed to start Azure continuous recognition.";
            std::cerr << "❌ [" << session_id << "] " << error_message << std::endl;
            stream_error_occurred.store(true);
            cleanup_resources(false, true); // LLM 스트림만 정리 시도
            return Status(StatusCode::INTERNAL, error_message);
        }
        azure_started.store(true);
        std::cout << "   [" << session_id << "] Azure recognition started successfully." << std::endl;


        // --- 5. 오디오 청크 읽기 루프 --- (PushAudioChunk 호출 가능해야 함)
        STTStreamRequest audio_request;
        size_t total_bytes_received = 0;
        std::cout << "   [" << session_id << "] Waiting for audio chunks from client..." << std::endl;
        while (!stream_error_occurred.load() && reader->Read(&audio_request)) {
            if (context->IsCancelled()) {
                 std::cout << "   [" << session_id << "] Client cancelled the request." << std::endl;
                 error_message = "Request cancelled by client.";
                 stream_error_occurred.store(true);
                 break;
            }

            if (audio_request.request_data_case() == STTStreamRequest::kAudioChunk) {
                const auto& chunk = audio_request.audio_chunk();
                if (!chunk.empty()) {
                    total_bytes_received += chunk.size();
                    // 헤더 수정 후 이 함수 호출이 가능해야 함
                    azure_stt_client_->PushAudioChunk(
                        reinterpret_cast<const uint8_t*>(chunk.data()),
                        chunk.size()
                    );
                }
            } // ... (else if 등 기존과 동일) ...
             else if (audio_request.request_data_case() == STTStreamRequest::REQUEST_DATA_NOT_SET) {
                 std::cerr << "⚠️ [" << session_id << "] Received request with data not set." << std::endl;
             }
              else {
                  std::cerr << "⚠️ [" << session_id << "] Received unexpected non-audio chunk data after config (type: "
                            << audio_request.request_data_case() << "). Ignoring." << std::endl;
              }
        } // end while

        // ... (루프 종료 원인 확인 - 기존과 동일) ...
        if (stream_error_occurred.load()) {
              std::cerr << "❌ [" << session_id << "] Error occurred, exiting audio reading loop. Reason: " << error_message << std::endl;
        } else {
              std::cout << "ℹ️ [" << session_id << "] Client finished sending audio. Total bytes received: " << total_bytes_received << "." << std::endl;
        }


        // --- 6. 클라이언트 오디오 스트림 종료 후 처리 --- (StopContinuousRecognition 호출 가능해야 함)
        if (azure_started.load()) {
            // 헤더 수정 후 이 함수 호출이 가능해야 함
            azure_stt_client_->StopContinuousRecognition();
        }


        // --- 7. Azure 처리 완료 대기 --- (변경 없음)
        std::cout << "   [" << session_id << "] Waiting for Azure STT processing to complete..." << std::endl;
        std::future_status wait_status = azure_processing_complete_future.wait_for(std::chrono::seconds(30));
        // ... (타임아웃 처리 - 기존과 동일) ...
        if (wait_status == std::future_status::timeout) {
             std::cerr << "❌ [" << session_id << "] Timed out waiting for Azure STT completion (30s)." << std::endl;
             if (!stream_error_occurred.load()) {
                 error_message = "Timeout waiting for Azure STT completion.";
                 stream_error_occurred.store(true);
             }
        } else {
             std::cout << "   [" << session_id << "] Azure STT processing completed or error signal received." << std::endl;
        }


        // --- 8. LLM 스트림 종료 (내부 수정됨) ---
        Status final_llm_status = Status::OK;
        if (llm_stream_started.load()) {
             std::cout << "   [" << session_id << "] Finishing LLM engine stream..." << std::endl;
             // 수정됨: Status 객체 하나만 받음
             grpc::Status llm_status = llm_engine_client_->FinishStream();
             final_llm_status = llm_status;
             llm_stream_started.store(false);

             if (!llm_status.ok()) {
                 std::cerr << "❌ [" << session_id << "] LLM stream finish error: (" << llm_status.error_code() << ") "
                           << llm_status.error_message() << std::endl;
                  if (!stream_error_occurred.load()) {
                     error_message = "Failed to finish LLM stream: " + llm_status.error_message();
                     stream_error_occurred.store(true);
                  }
             } else {
                  // 수정됨: llm_summary 관련 로깅 제거
                  std::cout << "   [" << session_id << "] LLM stream finished successfully." << std::endl;
                  // 필요하다면 서버가 Empty를 반환했음을 명시적으로 로깅
             }
        } else {
             std::cout << "   [" << session_id << "] LLM stream was not started or already finished, skipping finish." << std::endl;
        }


        // --- 9. 최종 상태 반환 --- (변경 없음)
        std::cout << "🏁 [" << session_id << "] Processing complete. Final status check." << std::endl;
        if (stream_error_occurred.load()) {
            if (context->IsCancelled()) {
                std::cerr << "❌ [" << session_id << "] Returning CANCELLED status." << std::endl;
                 cleanup_resources(true, llm_stream_started.load());
                return Status(StatusCode::CANCELLED, "Request cancelled by client during processing.");
            }
            if (!final_llm_status.ok() && final_llm_status.error_code() != StatusCode::CANCELLED) {
                 std::cerr << "❌ [" << session_id << "] Returning LLM finish error status: (" << final_llm_status.error_code() << ")" << std::endl;
                 cleanup_resources(true, false);
                 return final_llm_status;
            }
            std::cerr << "❌ [" << session_id << "] Returning INTERNAL error status: " << error_message << std::endl;
            cleanup_resources(true, llm_stream_started.load());
            return Status(StatusCode::INTERNAL, "An internal error occurred: " + error_message);
        } else {
            std::cout << "✅ [" << session_id << "] Returning OK status." << std::endl;
            return Status::OK;
        }

    } catch (const std::exception& e) {
        // ... (예외 처리 - 기존과 동일) ...
        std::cerr << "❌ [" << session_id << "] Unhandled exception in RecognizeStream: " << e.what() << std::endl;
        stream_error_occurred.store(true);
        error_message = "Unhandled exception: " + std::string(e.what());
        cleanup_resources(azure_started.load(), llm_stream_started.load());
        return Status(StatusCode::UNKNOWN, "An unknown exception occurred in the service handler.");
    } catch (...) {
        // ... (알 수 없는 예외 처리 - 기존과 동일) ...
         std::cerr << "❌ [" << session_id << "] Unknown non-standard exception in RecognizeStream." << std::endl;
         stream_error_occurred.store(true);
         error_message = "Unknown non-standard exception.";
         cleanup_resources(azure_started.load(), llm_stream_started.load());
         return Status(StatusCode::UNKNOWN, "An unknown exception occurred.");
    }
} // end RecognizeStream

} // namespace stt