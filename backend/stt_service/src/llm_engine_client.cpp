#include "llm_engine_client.h"
#include <iostream>
#include <chrono> // for sleep_for

namespace stt {

// 생성자
LLMEngineClient::LLMEngineClient(const std::string& server_address)
  : server_address_(server_address)
{
    // gRPC 채널 생성 (Insecure 예시, 필요시 SecureCredentials 사용)
    channel_ = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
    if (!channel_) {
        throw std::runtime_error("Failed to create gRPC channel to " + server_address);
    }
    // Stub 생성
    stub_ = LLMService::NewStub(channel_);
    if (!stub_) {
         throw std::runtime_error("Failed to create gRPC stub.");
    }
    std::cout << "ℹ️ LLMEngineClient created for address: " << server_address << std::endl;
}

// 소멸자
LLMEngineClient::~LLMEngineClient() {
    // 스트림이 아직 활성 상태이면 자동으로 종료 시도
    if (IsStreamActive()) {
        std::cerr << "⚠️ LLMEngineClient destroyed while stream was active. Finishing stream..." << std::endl;
        FinishStream(); // 결과 무시하고 정리 목적
    }
    std::cout << "ℹ️ LLMEngineClient destroyed." << std::endl;
}

// 스트림 시작
bool LLMEngineClient::StartStream(const std::string& session_id) {
    if (IsStreamActive()) {
        std::cerr << "⚠️ StartStream called while another stream is already active. Finish the previous stream first." << std::endl;
        return false;
    }

    session_id_ = session_id;
    context_ = std::make_unique<ClientContext>(); // 새로운 Context 생성
    // summary_response_ 멤버 변수 초기화 (선택적)
    summary_response_.Clear();

    // Client Streaming RPC 시작: Context와 최종 응답을 받을 변수(&summary_response_)를 전달합니다.
    // 이 함수 호출은 스트림 쓰기 객체(ClientWriter)의 unique_ptr를 반환합니다.
    stream_ = stub_->ProcessTextStream(context_.get(), &summary_response_);

    if (!stream_) {
        std::cerr << "❌ Failed to start gRPC stream to LLM engine." << std::endl;
        context_.reset(); // 실패 시 context 정리
        return false;
    }

    stream_active_.store(true); // 스트림 활성 상태로 설정
    std::cout << "✅ LLM engine stream started. Session ID: " << (session_id_.empty() ? "[empty]" : session_id_) << std::endl;
    return true;
}

// 텍스트 청크 전송
bool LLMEngineClient::SendTextChunk(const std::string& text, bool is_final) {
    if (!IsStreamActive()) {
        // 스트림 시작 전에 호출되거나 이미 종료된 후 호출된 경우
        std::cerr << "⚠️ SendTextChunk called but stream is not active." << std::endl;
        return false;
    }

    LLMStreamRequest request;
    request.set_session_id(session_id_);
    request.set_text_chunk(text);
    request.set_is_final(is_final);

    bool write_ok = false;
    {
        // 뮤텍스로 stream_ 객체 접근 보호
        std::lock_guard<std::mutex> lock(stream_mutex_);
        // stream_ 포인터 유효성 재확인 (다른 스레드에서 FinishStream 호출 가능성 고려)
        if (stream_) {
            write_ok = stream_->Write(request);
        }
    } // 뮤텍스 자동 해제

    if (!write_ok) {
        std::cerr << "❌ Failed to write text chunk to LLM engine stream. The stream might be broken." << std::endl;
        // 쓰기 실패는 보통 스트림이 끊어졌음을 의미하므로, 스트림을 비활성 상태로 변경
        stream_active_.store(false);
        // FinishStream()을 호출하여 에러 상태를 명시적으로 얻도록 유도할 수도 있음
        return false;
    }
    // 상세 로깅 필요 시 주석 해제
    // std::cout << "   Sent chunk to LLM: (final=" << is_final << ") '" << text.substr(0, 50) << "...'" << std::endl;
    return true;
}

// 스트림 종료 및 최종 응답 수신
std::pair<Status, LLMStreamSummary> LLMEngineClient::FinishStream() {
    if (!IsStreamActive()) {
         // 이미 종료되었거나 시작되지 않은 스트림
         std::cerr << "⚠️ FinishStream called but stream is not active or already finished." << std::endl;
         return {Status(grpc::StatusCode::FAILED_PRECONDITION, "Stream not active"), LLMStreamSummary()};
    }

    Status status;
    // summary_response_ 멤버 변수는 gRPC 라이브러리가 Finish() 호출 시 채워줍니다.

    {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        // 뮤텍스 내에서 stream_ 유효성 다시 확인
        if (!stream_) {
             // 이 경우는 거의 없어야 하지만 방어적으로 처리
             stream_active_.store(false);
             return {Status(grpc::StatusCode::INTERNAL, "Stream pointer became null unexpectedly before Finish"), LLMStreamSummary()};
        }

        // 1. 클라이언트 측 스트림 쓰기 완료 알림
        bool writes_done_ok = stream_->WritesDone();
        // WritesDone() 호출은 스트림 상태에 따라 false를 반환할 수 있지만,
        // 실패하더라도 Finish()는 호출해야 최종 상태를 얻을 수 있습니다.
        if (!writes_done_ok) {
             std::cerr << "⚠️ WritesDone failed on LLM stream (stream might already be broken)." << std::endl;
        }

        // 2. 서버의 최종 응답 및 상태 수신
        // 이 호출은 서버가 스트림 처리를 완료하고 LLMStreamSummary 응답을 보낼 때까지 블록됩니다.
        // gRPC는 이 응답을 StartStream 시 전달한 &summary_response_ 변수에 채웁니다.
        status = stream_->Finish();

        // 3. 스트림 및 컨텍스트 리소스 정리
        stream_.reset();
        context_.reset();

    } // 뮤텍스 자동 해제

    // 4. 스트림 비활성 상태로 설정
    stream_active_.store(false);

    // 5. 결과 로깅
    if (status.ok()) {
        std::cout << "✅ LLM engine stream finished successfully. Session ID: " << session_id_ << std::endl;
        // 수신된 요약 정보 로깅 (선택 사항)
        // std::cout << "   LLM Summary Received: Success=" << summary_response_.success()
        //           << ", Msg=" << summary_response_.message() << std::endl;
    } else {
        std::cerr << "❌ LLM engine stream finished with error (" << status.error_code()
                  << "): " << status.error_message() << ". Session ID: " << session_id_ << std::endl;
    }

    // 최종 상태와 서버로부터 받은 요약 응답 반환
    return {status, summary_response_};
}

// 스트림 활성 상태 확인
bool LLMEngineClient::IsStreamActive() const {
    // stream_ 포인터 유효성도 함께 체크하는 것이 더 안전할 수 있음
    // return stream_active_.load() && (stream_ != nullptr);
    return stream_active_.load();
}

} // namespace stt