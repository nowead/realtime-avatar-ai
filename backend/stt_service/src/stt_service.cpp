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
#include <thread> 

#include "llm_engine_client.h" 
#include "azure_stt_client.h" 
#include <google/protobuf/empty.pb.h> 
#include "stt.pb.h" 
#include "llm.pb.h" // llm::SessionConfig 사용을 위해 추가

using grpc::Status;
using grpc::StatusCode;

namespace stt {

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

Status STTServiceImpl::RecognizeStream(
    ServerContext* context,
    ServerReader<STTStreamRequest>* reader,
    google::protobuf::Empty* response 
) {
    const std::string client_peer = context->peer();
    std::string stt_internal_session_id; // STT 서비스 내부 세션 ID
    std::string frontend_session_id;     // 프론트엔드 웹소켓 세션 ID

    std::cout << "✅ STT_Service: New client connection from: " << client_peer << std::endl;

    std::string language;
    std::atomic<bool> azure_started{false};
    std::atomic<bool> llm_stream_started{false};
    std::atomic<bool> stream_error_occurred{false};
    std::string error_message_detail; // 상세 오류 메시지 저장용

    std::promise<void> azure_processing_complete_promise;
    auto azure_processing_complete_future = azure_processing_complete_promise.get_future();

    auto cleanup_resources = [&](bool stop_azure, bool finish_llm) {
         std::cout << "🧹 STT_Service [STT_SID:" << (stt_internal_session_id.empty() ? "NO_STT_SID" : stt_internal_session_id) 
                   << ", FE_SID:" << (frontend_session_id.empty() ? "NO_FE_SID" : frontend_session_id)
                   << "] Cleaning up resources... StopAzure=" << stop_azure << ", FinishLLM=" << finish_llm << std::endl;
         if (finish_llm && llm_engine_client_ && llm_stream_started.load() && llm_engine_client_->IsStreamActive()) { // IsStreamActive 추가
             std::cout << "   Finishing LLM stream for FE_SID [" << frontend_session_id << "]..." << std::endl;
             grpc::Status llm_status = llm_engine_client_->FinishStream();
             if (!llm_status.ok()) {
                  std::cerr << "   ⚠️ LLM stream finish error during cleanup: (" << llm_status.error_code() << ") "
                            << llm_status.error_message() << std::endl;
             } else {
                  std::cout << "   LLM stream finished successfully during cleanup for FE_SID [" << frontend_session_id << "]." << std::endl;
             }
             llm_stream_started.store(false);
         }
         if (stop_azure && azure_stt_client_ && azure_started.load()) {
              std::cout << "   Stopping Azure recognition for STT_SID [" << stt_internal_session_id << "]..." << std::endl;
              azure_stt_client_->StopContinuousRecognition();
              azure_started.store(false);
         }
    };

    try {
        STTStreamRequest initial_request;
        std::cout << "   STT_Service: Waiting for initial RecognitionConfig from client " << client_peer << "..." << std::endl;
        if (!reader->Read(&initial_request)) {
            error_message_detail = "Failed to read initial request from client.";
            std::cerr << "❌ STT_Service [Peer:" << client_peer << "] " << error_message_detail << std::endl;
            return Status(StatusCode::INVALID_ARGUMENT, error_message_detail);
        }
        
        if (initial_request.request_data_case() != STTStreamRequest::kConfig) {
            error_message_detail = "Initial request must be RecognitionConfig.";
            std::cerr << "❌ STT_Service [Peer:" << client_peer << "] " << error_message_detail << std::endl;
            return Status(StatusCode::INVALID_ARGUMENT, error_message_detail);
        }

        const auto& received_config = initial_request.config();
        language = received_config.language();
        frontend_session_id = received_config.frontend_session_id(); // ★ frontend_session_id 추출
        stt_internal_session_id = received_config.session_id();      // STT 내부용 세션 ID (websocket_gateway가 전달)
        
        if (stt_internal_session_id.empty()) { // 혹시 websocket_gateway가 안줬다면 생성
            stt_internal_session_id = generate_uuid();
        }

        // ★ frontend_session_id 유효성 검사 (필수)
        if (frontend_session_id.empty()) {
            error_message_detail = "CRITICAL: frontend_session_id is missing in RecognitionConfig from websocket_gateway.";
            std::cerr << "❌ STT_Service [STT_SID:" << stt_internal_session_id << "] " << error_message_detail << std::endl;
            // 이 경우, 클라이언트(websocket_gateway)에게 오류를 알리고 스트림을 종료해야 함
            return Status(StatusCode::INVALID_ARGUMENT, error_message_detail);
        }
        if (language.empty()) {
           error_message_detail = "Language code is missing in RecognitionConfig.";
           std::cerr << "❌ STT_Service [STT_SID:" << stt_internal_session_id << ", FE_SID:" << frontend_session_id << "] " << error_message_detail << std::endl;
           return Status(StatusCode::INVALID_ARGUMENT, error_message_detail);
        }
        std::cout << "   STT_Service [STT_SID:" << stt_internal_session_id 
                  << ", FE_SID:" << frontend_session_id 
                  << "] Config received: Language=" << language 
                  << ", Received STT SID from Gateway: " << received_config.session_id() 
                  << ", Received FE SID from Gateway: " << received_config.frontend_session_id() << std::endl;
        std::cout << "   STT_Service: Received RecognitionConfig (ShortDebugString): " << received_config.ShortDebugString() << std::endl; // 상세 로깅


        std::cout << "   STT_Service [STT_SID:" << stt_internal_session_id << "] Starting stream to LLM Engine for FE_SID [" << frontend_session_id << "]..." << std::endl;
        llm::SessionConfig llm_config_to_send; 
        llm_config_to_send.set_frontend_session_id(frontend_session_id);       
        llm_config_to_send.set_session_id(stt_internal_session_id); // STT의 내부 세션 ID를 LLM의 내부 세션 ID로 전달 (선택적)
        
        std::cout << "   STT_Service: Sending SessionConfig to LLM: " << llm_config_to_send.ShortDebugString() << std::endl; // 전송 전 로깅

        if (!llm_engine_client_->StartStream(llm_config_to_send)) {
            error_message_detail = "Failed to start stream to LLM Engine.";
            std::cerr << "❌ STT_Service [STT_SID:" << stt_internal_session_id << ", FE_SID:" << frontend_session_id << "] " << error_message_detail << std::endl;
            stream_error_occurred.store(true);
            // cleanup_resources는 이 함수의 끝에서 호출되므로, 여기서는 상태만 설정
            return Status(StatusCode::INTERNAL, error_message_detail);
        }
        llm_stream_started.store(true);
        std::cout << "   STT_Service [STT_SID:" << stt_internal_session_id << "] LLM stream started successfully for FE_SID [" << frontend_session_id << "]." << std::endl;


        auto text_callback =
            [this, stt_sid = stt_internal_session_id, fe_sid = frontend_session_id, &stream_error_occurred, &error_message_detail, llm_client = llm_engine_client_]
            (const std::string& text, bool is_final) { 
            if (stream_error_occurred.load()) return;
            // std::cout << "   STT_Service [STT_SID:" << stt_sid << ", FE_SID:" << fe_sid << "] Azure Text (is_final=" << is_final << "): '" << text << "'" << std::endl;
            if (!llm_client->SendTextChunk(text)) { // SendTextChunk는 is_final 인자를 받지 않음
                std::cerr << "❌ STT_Service [STT_SID:" << stt_sid << ", FE_SID:" << fe_sid << "] Error sending text chunk to LLM Engine. Marking stream as error." << std::endl;
                if (!stream_error_occurred.load()) {
                     error_message_detail = "Failed to forward text chunk to LLM engine.";
                     stream_error_occurred.store(true);
                }
            }
        };

        auto completion_callback =
            [this, stt_sid = stt_internal_session_id, fe_sid = frontend_session_id, &stream_error_occurred, &error_message_detail, &azure_processing_complete_promise]
            (bool success, const std::string& azure_msg) {
             std::cout << "ℹ️ STT_Service [STT_SID:" << stt_sid << ", FE_SID:" << fe_sid << "] Azure STT processing finished. Success: " << success << std::endl;
             if (!success) {
                 std::cerr << "   Azure STT Error: " << azure_msg << std::endl;
                 if (!stream_error_occurred.load()) {
                     error_message_detail = "Azure STT recognition failed: " + azure_msg;
                     stream_error_occurred.store(true);
                 }
             }
             try {
                 azure_processing_complete_promise.set_value();
             } catch (const std::future_error& e) {
                  std::cerr << "ℹ️ STT_Service [STT_SID:" << stt_sid << "] Promise already set in completion_callback: " << e.what() << std::endl;
             }
        };

        std::cout << "   STT_Service [STT_SID:" << stt_internal_session_id << "] Starting Azure continuous recognition..." << std::endl;
        if (!azure_stt_client_->StartContinuousRecognition(language, text_callback, completion_callback)) {
            error_message_detail = "Failed to start Azure continuous recognition.";
            std::cerr << "❌ STT_Service [STT_SID:" << stt_internal_session_id << ", FE_SID:" << frontend_session_id << "] " << error_message_detail << std::endl;
            stream_error_occurred.store(true);
            // cleanup_resources(false, true); // LLM 스트림만 정리 시도 (함수 끝에서 일괄 처리)
            return Status(StatusCode::INTERNAL, error_message_detail);
        }
        azure_started.store(true);
        std::cout << "   STT_Service [STT_SID:" << stt_internal_session_id << "] Azure recognition started successfully." << std::endl;

        STTStreamRequest audio_request;
        size_t total_bytes_received = 0;
        std::cout << "   STT_Service [STT_SID:" << stt_internal_session_id << "] Waiting for audio chunks from client..." << std::endl;
        while (!stream_error_occurred.load() && reader->Read(&audio_request)) {
            if (context->IsCancelled()) {
                 std::cout << "   STT_Service [STT_SID:" << stt_internal_session_id << "] Client cancelled the request." << std::endl;
                 error_message_detail = "Request cancelled by client.";
                 stream_error_occurred.store(true);
                 break;
            }

            if (audio_request.request_data_case() == STTStreamRequest::kAudioChunk) {
                const auto& chunk_data_str = audio_request.audio_chunk(); // proto bytes is std::string
                if (!chunk_data_str.empty()) {
                    total_bytes_received += chunk_data_str.size();
                    azure_stt_client_->PushAudioChunk(
                        reinterpret_cast<const uint8_t*>(chunk_data_str.data()),
                        chunk_data_str.size()
                    );
                }
            } else if (audio_request.request_data_case() == STTStreamRequest::REQUEST_DATA_NOT_SET) {
                 std::cerr << "⚠️ STT_Service [STT_SID:" << stt_internal_session_id << "] Received request with data not set." << std::endl;
            } else { // Config가 또 들어온 경우 등
                  std::cerr << "⚠️ STT_Service [STT_SID:" << stt_internal_session_id << "] Received unexpected non-audio chunk data after config (type: "
                            << audio_request.request_data_case() << "). Ignoring." << std::endl;
            }
        } 

        if (stream_error_occurred.load()) {
              std::cerr << "❌ STT_Service [STT_SID:" << stt_internal_session_id << ", FE_SID:" << frontend_session_id 
                        << "] Error occurred, exiting audio reading loop. Reason: " << error_message_detail << std::endl;
        } else {
              std::cout << "ℹ️ STT_Service [STT_SID:" << stt_internal_session_id << ", FE_SID:" << frontend_session_id 
                        << "] Client finished sending audio. Total bytes received: " << total_bytes_received << "." << std::endl;
        }

        if (azure_started.load()) {
            std::cout << "   STT_Service [STT_SID:" << stt_internal_session_id << "] Signaling Azure to stop continuous recognition." << std::endl;
            azure_stt_client_->StopContinuousRecognition();
        }

        std::cout << "   STT_Service [STT_SID:" << stt_internal_session_id << "] Waiting for Azure STT processing to complete..." << std::endl;
        std::future_status wait_status = azure_processing_complete_future.wait_for(std::chrono::seconds(30));
        if (wait_status == std::future_status::timeout) {
             std::cerr << "❌ STT_Service [STT_SID:" << stt_internal_session_id << ", FE_SID:" << frontend_session_id 
                       << "] Timed out waiting for Azure STT completion (30s)." << std::endl;
             if (!stream_error_occurred.load()) {
                 error_message_detail = "Timeout waiting for Azure STT completion.";
                 stream_error_occurred.store(true);
             }
        } else {
             std::cout << "   STT_Service [STT_SID:" << stt_internal_session_id << "] Azure STT processing completed or error signal received." << std::endl;
        }

        Status final_llm_status = Status::OK;
        if (llm_stream_started.load()) {
             std::cout << "   STT_Service [STT_SID:" << stt_internal_session_id << "] Finishing LLM engine stream for FE_SID [" << frontend_session_id << "]..." << std::endl;
             grpc::Status llm_status = llm_engine_client_->FinishStream();
             final_llm_status = llm_status;
             llm_stream_started.store(false);

             if (!llm_status.ok()) {
                 std::cerr << "❌ STT_Service [STT_SID:" << stt_internal_session_id << ", FE_SID:" << frontend_session_id 
                           << "] LLM stream finish error: (" << llm_status.error_code() << ") "
                           << llm_status.error_message() << std::endl;
                  if (!stream_error_occurred.load()) {
                     error_message_detail = "Failed to finish LLM stream: " + llm_status.error_message();
                     stream_error_occurred.store(true);
                  }
             } else {
                  std::cout << "   STT_Service [STT_SID:" << stt_internal_session_id << "] LLM stream finished successfully for FE_SID [" << frontend_session_id << "]." << std::endl;
             }
        } else {
             std::cout << "   STT_Service [STT_SID:" << stt_internal_session_id << "] LLM stream was not started or already finished, skipping finish." << std::endl;
        }

        std::cout << "🏁 STT_Service [STT_SID:" << stt_internal_session_id << ", FE_SID:" << frontend_session_id << "] Processing complete. Final status check." << std::endl;
        if (stream_error_occurred.load()) {
            if (context->IsCancelled()) { // 클라이언트가 명시적으로 취소한 경우
                std::cerr << "❌ STT_Service [STT_SID:" << stt_internal_session_id << "] Returning CANCELLED status due to client cancellation." << std::endl;
                 // cleanup_resources는 finally 블록처럼 여기서 호출될 것임
                return Status(StatusCode::CANCELLED, "Request cancelled by client during processing: " + error_message_detail);
            }
            // LLM 스트림 종료 오류가 최종 오류인 경우 해당 상태 반환
            if (!final_llm_status.ok() && final_llm_status.error_code() != StatusCode::CANCELLED) { // LLM 오류가 취소가 아니라면
                 std::cerr << "❌ STT_Service [STT_SID:" << stt_internal_session_id << "] Returning LLM finish error status: (" 
                           << final_llm_status.error_code() << ")" << std::endl;
                 return final_llm_status;
            }
            // 그 외 내부 오류
            std::cerr << "❌ STT_Service [STT_SID:" << stt_internal_session_id << "] Returning INTERNAL error status: " << error_message_detail << std::endl;
            return Status(StatusCode::INTERNAL, "An internal error occurred in STT service: " + error_message_detail);
        } else {
            std::cout << "✅ STT_Service [STT_SID:" << stt_internal_session_id << "] Returning OK status." << std::endl;
            return Status::OK;
        }

    } catch (const std::exception& e) {
        std::cerr << "❌ STT_Service [STT_SID:" << (stt_internal_session_id.empty() ? "N/A" : stt_internal_session_id) 
                  << ", FE_SID:" << (frontend_session_id.empty() ? "N/A" : frontend_session_id)
                  << "] Unhandled exception in RecognizeStream: " << e.what() << std::endl;
        stream_error_occurred.store(true); // 오류 플래그 설정
        error_message_detail = "Unhandled exception: " + std::string(e.what());
        // cleanup_resources는 함수 종료 시 호출됨
        return Status(StatusCode::UNKNOWN, "An unknown exception occurred in the STT service handler.");
    } catch (...) {
         std::cerr << "❌ STT_Service [STT_SID:" << (stt_internal_session_id.empty() ? "N/A" : stt_internal_session_id) 
                   << ", FE_SID:" << (frontend_session_id.empty() ? "N/A" : frontend_session_id)
                   << "] Unknown non-standard exception in RecognizeStream." << std::endl;
         stream_error_occurred.store(true);
         error_message_detail = "Unknown non-standard exception.";
         return Status(StatusCode::UNKNOWN, "An unknown non-standard exception occurred.");
    }
}

} // namespace stt
