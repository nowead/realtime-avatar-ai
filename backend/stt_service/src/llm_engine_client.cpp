#include "llm_engine_client.h"
#include <iostream>
#include <chrono> // for sleep_for
#include <google/protobuf/empty.pb.h> // Empty 타입 사용 위해 필요할 수 있음

namespace stt {

// 생성자 (변경 없음)
LLMEngineClient::LLMEngineClient(const std::string& server_address)
  : server_address_(server_address)
{
    try {
        channel_ = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
        if (!channel_) {
            throw std::runtime_error("Failed to create gRPC channel to " + server_address);
        }
        stub_ = llm::LLMService::NewStub(channel_); // 네임스페이스 명시 (llm::)
        if (!stub_) {
             throw std::runtime_error("Failed to create LLMService::Stub.");
        }
    } catch (const std::exception& e) {
        std::cerr << "❌ Exception in LLMEngineClient constructor: " << e.what() << std::endl;
        throw;
    }
    std::cout << "  LLMEngineClient initialized for address: " << server_address << std::endl;
}

// 소멸자 (summary_response_ 관련 내용 없음, 변경 없음)
LLMEngineClient::~LLMEngineClient() {
    std::cout << "ℹ️ Destroying LLMEngineClient..." << std::endl;
    if (IsStreamActive()) {
        std::cerr << "⚠️ WARNING: LLMEngineClient destroyed while stream was active for session ["
                  << session_id_ << "]. Attempting to finish stream..." << std::endl;
        try {
            FinishStream(); // 반환값(Status)은 무시
        } catch(const std::exception& e) {
            std::cerr << "   Exception during cleanup in destructor: " << e.what() << std::endl;
        }
    }
     stub_.reset();
     channel_.reset();
    std::cout << "✅ LLMEngineClient destroyed." << std::endl;
}

// 스트림 시작 및 초기 설정 전송 (로직 수정됨)
bool LLMEngineClient::StartStream(const llm::SessionConfig& config_from_stt) { // ★ 수정
    std::lock_guard<std::mutex> lock(stream_mutex_);

    if (stream_active_.load()) {
        std::cerr << "⚠️ LLM Client: StartStream called while another stream is already active. Finish previous stream." << std::endl;
        return false;
    }
    // config_from_stt.frontend_session_id() 또는 config_from_stt.session_id() 유효성 검사 추가 가능
    if (config_from_stt.frontend_session_id().empty()) {
        std::cerr << "❌ LLM Client: StartStream called with empty frontend_session_id in config." << std::endl;
        return false;
    }

    // 내부 로깅이나 추적을 위해 세션 ID 저장 (frontend_session_id를 사용할지, config_from_stt.session_id()를 사용할지 결정)
    session_id_ = config_from_stt.frontend_session_id(); // 예시: frontend_session_id를 내부 대표 ID로 사용
    std::cout << "⏳ LLM Client: Starting stream for frontend_session_id [" << session_id_ << "] (LLM internal session: " << config_from_stt.session_id() << ")..." << std::endl;

    context_ = std::make_unique<ClientContext>();
    stream_ = stub_->ProcessTextStream(context_.get(), &server_response_);

    if (!stream_) {
        std::cerr << "❌ LLM Client: Failed to initiate gRPC stream to LLM engine for frontend_session_id [" << session_id_ << "]." << std::endl;
        context_.reset();
        session_id_.clear();
        return false;
    }

    llm::LLMStreamRequest initial_llm_request;
    // 전달받은 SessionConfig 객체를 그대로 LLMStreamRequest의 config 필드에 설정
    initial_llm_request.mutable_config()->CopyFrom(config_from_stt); // ★ 중요: frontend_session_id가 포함된 config 전달

    if (!stream_->Write(initial_llm_request)) {
        std::cerr << "❌ LLM Client: Failed to write initial SessionConfig for frontend_session_id [" << session_id_ << "]. Finishing stream." << std::endl;
        // ... 오류 처리 ...
        return false;
    }

    stream_active_.store(true);
    std::cout << "✅ LLM Client: Stream successfully started and SessionConfig sent for frontend_session_id [" << session_id_ << "]." << std::endl;
    return true;
}

// 텍스트 청크 전송 (is_final 제거, oneof 사용)
bool LLMEngineClient::SendTextChunk(const std::string& text) { // 수정됨: is_final 파라미터 제거
    if (!IsStreamActive()) {
        std::cerr << "⚠️ LLM Client: SendTextChunk called but stream is not active for session [" << session_id_ << "]." << std::endl;
        return false;
    }

    llm::LLMStreamRequest request; // 네임스페이스 명시
    // request.set_session_id(session_id_); // 수정됨: 제거
    request.set_text_chunk(text); // 수정됨: oneof 필드 설정
    // request.set_is_final(is_final); // 수정됨: 제거

    bool write_ok = false;
    {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        if (stream_) {
            // 로그 필요 시: std::cout << "   LLM Client: Sending chunk (session=" << session_id_ << "): '" << ... << std::endl;
            write_ok = stream_->Write(request);
        } else {
             write_ok = false;
        }
    }

    if (!write_ok) {
        std::cerr << "❌ LLM Client: Failed to write text chunk to LLM engine stream for session [" << session_id_
                  << "]. Marking as inactive." << std::endl;
        stream_active_.store(false); // 쓰기 실패 시 비활성 처리 (FinishStream에서 최종 상태 확인)
        return false;
    }
    return true;
}

// 스트림 종료 및 최종 상태 수신 (반환 타입 및 로직 수정됨)
Status LLMEngineClient::FinishStream() { // 수정됨: 반환 타입 Status
    std::lock_guard<std::mutex> lock(stream_mutex_);

    if (!stream_active_.load() || !stream_) {
         std::cerr << "⚠️ LLM Client: FinishStream called but stream is not active or already finished for session [" << session_id_ << "]." << std::endl;
         // return {Status(grpc::StatusCode::FAILED_PRECONDITION, "Stream not active or already finished"), LLMStreamSummary()}; // 수정됨: 아래처럼 변경
         return Status(grpc::StatusCode::FAILED_PRECONDITION, "Stream not active or already finished");
    }

     std::cout << "⏳ LLM Client: Finishing stream for session [" << session_id_ << "]..." << std::endl;

    bool writes_done_ok = stream_->WritesDone();
    if (!writes_done_ok) {
         std::cerr << "⚠️ LLM Client: WritesDone failed on LLM stream for session [" << session_id_
                   << "] (stream might already be broken)." << std::endl;
    } else {
         std::cout << "   LLM Client: WritesDone called successfully for session [" << session_id_ << "]." << std::endl;
    }

     std::cout << "   LLM Client: Waiting for final status (Finish) from server for session [" << session_id_ << "]..." << std::endl;
    Status status = stream_->Finish(); // 최종 상태 수신

    stream_.reset();
    context_.reset();
    stream_active_.store(false);
    std::string current_session_id = session_id_;
    session_id_.clear();

    if (status.ok()) {
        std::cout << "✅ LLM Client: Stream finished successfully for session [" << current_session_id << "]. Server returned Empty." << std::endl;
        // summary_response_ 관련 로깅 제거됨
    } else {
        std::cerr << "❌ LLM Client: Stream finished with error for session [" << current_session_id
                  << "]. Status: (" << status.error_code() << "): " << status.error_message() << std::endl;
    }

    // 최종 상태 반환 (summary_response_ 제거됨)
    // return {status, summary_response_}; // 수정됨: 아래처럼 변경
    return status;
}

// 스트림 활성 상태 확인 (변경 없음)
bool LLMEngineClient::IsStreamActive() const {
    std::lock_guard<std::mutex> lock(stream_mutex_);
    return stream_active_.load() && (stream_ != nullptr);
}

} // namespace stt