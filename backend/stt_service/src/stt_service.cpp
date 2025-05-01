#include "stt_service.h"
#include <iostream>
#include <string>
#include <vector>
#include <future>    // std::promise, std::future
#include <atomic>    // std::atomic_bool
#include <random>    // For basic UUID generation
#include <sstream>   // For basic UUID generation
#include <iomanip>   // For basic UUID generation
#include <chrono>    // std::chrono::*

namespace stt {

// 간단한 UUID 생성 함수 (예시)
std::string generate_uuid() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;

    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(16) << dis(gen); // 64 bits
    ss << std::setw(16) << dis(gen); // 64 bits
    return ss.str();
}


// 생성자: 의존성 주입
STTServiceImpl::STTServiceImpl(std::shared_ptr<AzureSTTClient> azure_client,
                               std::shared_ptr<LLMEngineClient> llm_client)
  : azure_stt_client_(azure_client), llm_engine_client_(llm_client)
{
    if (!azure_stt_client_) {
        throw std::runtime_error("AzureSTTClient cannot be null.");
    }
    if (!llm_engine_client_) {
        throw std::runtime_error("LLMEngineClient cannot be null.");
    }
}

// Client Streaming RPC 구현
Status STTServiceImpl::RecognizeStream(
    ServerContext* context,
    ServerReader<STTStreamRequest>* reader,
    Empty* response // 최종 응답 (내용 없음)
) {
    std::string client_peer = context->peer();
    std::cout << "✅ New client connected for STT: " << client_peer << std::endl;

    std::string language;
    std::string session_id = generate_uuid(); // 각 스트림마다 고유 ID 생성
    std::cout << "   Session ID: " << session_id << std::endl;

    // 비동기 작업 완료 시그널 및 오류 추적용 변수
    std::promise<void> processing_complete_promise;
    auto processing_complete_future = processing_complete_promise.get_future();
    std::atomic<bool> critical_error_occurred(false);
    std::atomic<bool> llm_stream_started(false); // LLM 스트림 시작 여부 추적

    // --- 1. 첫 메시지 (Config) 읽기 ---
    STTStreamRequest initial_request;
    if (!reader->Read(&initial_request)) {
        std::cerr << "❌ Failed to read initial request from client: " << client_peer << std::endl;
        return Status(grpc::StatusCode::INVALID_ARGUMENT, "Failed to read initial request.");
    }

    if (initial_request.request_data_case() == STTStreamRequest::kConfig) {
        language = initial_request.config().language();
        if (language.empty()) {
            std::cerr << "❌ Language code is missing in RecognitionConfig." << std::endl;
             return Status(grpc::StatusCode::INVALID_ARGUMENT, "Language code is required.");
        }
         std::cout << "   Config received: Language=" << language << std::endl;
    } else {
        std::cerr << "❌ Initial request is not RecognitionConfig." << std::endl;
        return Status(grpc::StatusCode::INVALID_ARGUMENT, "Initial request must be RecognitionConfig.");
    }

    // --- 2. LLM Engine 스트림 시작 ---
    if (!llm_engine_client_->StartStream(session_id)) {
        std::cerr << "❌ Failed to start stream to LLM Engine for session: " << session_id << std::endl;
        critical_error_occurred.store(true);
        // LLM 스트림 시작 실패 시 즉시 종료
         return Status(grpc::StatusCode::INTERNAL, "Failed to connect to LLM engine.");
    }
    llm_stream_started.store(true);


    // --- 3. Azure STT 콜백 정의 ---
    // TextChunkCallback: Azure에서 텍스트 조각 받을 때마다 LLM 클라이언트로 전송
    auto text_callback =
        [this, &critical_error_occurred, session_id](const std::string& text, bool is_final) {
        if (critical_error_occurred.load()) return; // 이미 오류 발생 시 추가 전송 중단

        //std::cout << "   Forwarding to LLM: (final=" << is_final << ") '" << text.substr(0,30) << "...'" << std::endl;
        if (!llm_engine_client_->SendTextChunk(text, is_final)) {
            std::cerr << "❌ Error sending text chunk to LLM Engine for session: " << session_id << ". Marking as error." << std::endl;
            critical_error_occurred.store(true);
            // 여기서 Azure 인식 중단 시도? (복잡할 수 있음)
            // azure_stt_client_->StopContinuousRecognition(); // 콜백 내에서 호출 시 데드락 주의
        }
    };

    // RecognitionCompletionCallback: Azure 인식 전체가 완료/실패했을 때 호출됨
    auto completion_callback =
        [this, &critical_error_occurred, &processing_complete_promise, session_id, &llm_stream_started]
        (bool success, const std::string& error_msg) {

        std::cout << "ℹ️ Azure STT processing finished for session: " << session_id << ". Success: " << success << std::endl;
        if (!success) {
            std::cerr << "   Azure STT Error: " << error_msg << std::endl;
            critical_error_occurred.store(true);
        }

        // Azure 처리가 끝나면, LLM 스트림도 종료해야 함
        if(llm_stream_started.load()) { // LLM 스트림이 성공적으로 시작된 경우에만 종료 시도
            std::cout << "   Finishing LLM engine stream for session: " << session_id << std::endl;
            auto [llm_status, llm_summary] = llm_engine_client_->FinishStream();
            if (!llm_status.ok()) {
                 std::cerr << "   LLM stream finish error: (" << llm_status.error_code() << ") "
                           << llm_status.error_message() << std::endl;
                critical_error_occurred.store(true); // LLM 종료 실패도 전체 오류로 간주
            } else {
                 std::cout << "   LLM stream finished. Summary success: " << llm_summary.success() << std::endl;
                 // LLM 요약 결과 자체의 success 플래그에 따른 오류 처리 추가 가능
                 if (!llm_summary.success()) {
                      std::cerr << "   LLM processing summary indicates failure: " << llm_summary.message() << std::endl;
                      // critical_error_occurred.store(true); // LLM 내부 실패를 gRPC 오류로 반환할지 결정
                 }
            }
        } else {
             std::cout << "   LLM stream was not started, skipping FinishStream." << std::endl;
        }


        // 모든 처리가 완료되었음을 메인 스레드에 알림
        try {
            processing_complete_promise.set_value();
        } catch (const std::future_error& e) {
            // 이미 promise가 설정된 경우 (예: 매우 빠른 오류 발생 시)
             std::cerr << "ℹ️ Promise already set in completion_callback: " << e.what() << std::endl;
        }
    };

    // --- 4. Azure STT 인식 시작 ---
    if (!azure_stt_client_->StartContinuousRecognition(language, text_callback, completion_callback)) {
        std::cerr << "❌ Failed to start Azure continuous recognition for session: " << session_id << std::endl;
        critical_error_occurred.store(true);
        // Azure 시작 실패 시, 시작된 LLM 스트림이 있다면 종료 시도
        if(llm_stream_started.load()) {
             llm_engine_client_->FinishStream(); // 결과는 무시하고 정리 목적
        }
        // 완료 신호 보내서 즉시 종료
        try { processing_complete_promise.set_value(); } catch (...) {}
        return Status(grpc::StatusCode::INTERNAL, "Failed to start Azure speech recognition.");
    }

    // --- 5. 오디오 청크 읽기 루프 ---
    STTStreamRequest audio_request;
    size_t total_bytes_received = 0;
    std::cout << "   Waiting for audio chunks from client: " << client_peer << std::endl;
    while (reader->Read(&audio_request)) {
        if (critical_error_occurred.load()) {
             std::cerr << "   Critical error occurred, stopping audio reading loop." << std::endl;
             break; // 오류 발생 시 루프 중단
        }
         if (context->IsCancelled()) {
             std::cout << "   Client cancelled the request." << std::endl;
             critical_error_occurred.store(true); // 클라이언트 취소도 오류로 간주 가능
             break;
         }

        if (audio_request.request_data_case() == STTStreamRequest::kAudioChunk) {
            const auto& chunk = audio_request.audio_chunk();
            if (!chunk.empty()) {
                //std::cout << "   Received audio chunk: " << chunk.size() << " bytes" << std::endl;
                total_bytes_received += chunk.size();
                azure_stt_client_->PushAudioChunk(
                    reinterpret_cast<const uint8_t*>(chunk.data()),
                    chunk.size()
                );
            }
        } else {
             std::cerr << "⚠️ Received non-audio chunk data after config." << std::endl;
             // 오류로 처리할 수도 있음
        }
    }

    // --- 6. 클라이언트 오디오 스트림 종료 처리 ---
    std::cout << "ℹ️ Client finished sending audio or stream ended. Total bytes: " << total_bytes_received << ". Session: " << session_id << std::endl;

    // Azure STT에게 오디오 입력 종료 알림 (이렇게 하면 결국 completion_callback 호출됨)
    // 이미 오류가 발생했더라도 Stop 호출 시도 (리소스 정리 목적)
    azure_stt_client_->StopContinuousRecognition();


    // --- 7. 모든 비동기 처리 완료 대기 ---
    std::cout << "   Waiting for Azure STT and LLM forwarding to complete..." << std::endl;
    // completion_callback에서 promise가 set_value 될 때까지 대기
    // 타임아웃 설정 가능: processing_complete_future.wait_for(std::chrono::seconds(30));
    processing_complete_future.wait();


    // --- 8. 최종 상태 반환 ---
    std::cout << "✅ Processing complete for session: " << session_id << ". Final status check." << std::endl;
    if (critical_error_occurred.load()) {
        std::cerr << "❌ Returning INTERNAL error status due to processing errors." << std::endl;
        // 클라이언트가 취소한 경우 CANCELLED 상태 반환 고려
         if (context->IsCancelled()) {
             return Status(grpc::StatusCode::CANCELLED, "Request cancelled by client during processing.");
         }
        return Status(grpc::StatusCode::INTERNAL, "An internal error occurred during STT processing or LLM forwarding.");
    } else {
        std::cout << "✅ Returning OK status." << std::endl;
        return Status::OK;
    }
}

} // namespace stt