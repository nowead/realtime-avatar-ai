#include "tts_client.h"

#include <vector>
#include <cstdint>
#include <iostream>
#include <chrono>
#include <stdexcept>

namespace llm_engine {

// 실제 채널을 사용하는 생성자
TTSClient::TTSClient(std::shared_ptr<grpc::Channel> channel)
    : channel_(channel) {
    if (!channel_) {
        throw std::runtime_error("TTSClient: Provided gRPC channel is null.");
    }
    // 실제 서비스 이름 TTSService 사용
    stub_ = TTSService::NewStub(channel_);
    if (!stub_) {
        throw std::runtime_error("TTSClient: Failed to create TTSService::Stub.");
    }
    std::cout << "TTSClient initialized with real gRPC channel." << std::endl;
}

// 테스트용 Stub을 사용하는 생성자
TTSClient::TTSClient(std::shared_ptr<TTSService::StubInterface> stub) // 실제 서비스 이름 사용
    : stub_(stub) {
    if (!stub_) {
        throw std::runtime_error("TTSClient: Provided gRPC stub is null.");
    }
    std::cout << "TTSClient initialized with provided stub (for testing)." << std::endl;
}

// 스트림 시작 함수
bool TTSClient::StartStream(const std::string& session_id, const std::string& language_code, const std::string& voice_name) {
    std::lock_guard<std::mutex> lock(stream_mutex_);

    if (stream_active_.load()) {
        std::cerr << "⚠️ TTS Client: StartStream called while another stream [" << session_id_ << "] is already active." << std::endl;
        return false;
    }
    if (session_id.empty() || language_code.empty()) {
        std::cerr << "❌ TTS Client: StartStream called with empty session_id or language_code." << std::endl;
        return false;
    }
    if (!stub_) {
         std::cerr << "❌ TTS Client: StartStream called but stub_ is null." << std::endl;
         return false;
    }

    session_id_ = session_id;
    std::cout << "⏳ TTS Client: Starting stream for session [" << session_id_ << "]..." << std::endl;

    context_ = std::make_unique<ClientContext>();
    // 필요시 deadline 설정
    // auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(30);
    // context_->set_deadline(deadline);

    // SynthesizeStream RPC 호출 (TTSService::StubInterface 사용)
    stream_ = stub_->SynthesizeStream(context_.get(), &server_response_);

    if (!stream_) {
        std::cerr << "❌ TTS Client: Failed to initiate gRPC stream to TTS engine for session [" << session_id_ << "]." << std::endl;
        context_.reset();
        session_id_.clear();
        return false;
    }

    // 초기 SynthesisConfig 메시지 전송
    std::cout << "   TTS Client: Sending initial SynthesisConfig for session [" << session_id_ << "]..." << std::endl;
    TTSStreamRequest config_request; // 네임스페이스 불필요 (using 선언됨)
    SynthesisConfig* config = config_request.mutable_config(); // 네임스페이스 불필요 (using 선언됨)
    config->set_session_id(session_id_);
    config->set_language_code(language_code);
    config->set_voice_name(voice_name);

    if (!stream_->Write(config_request)) {
        std::cerr << "❌ TTS Client: Failed to write initial SynthesisConfig for session [" << session_id_ << "]. Finishing stream." << std::endl;
        grpc::Status finish_status = stream_->Finish(); // 스트림 종료 시도
        if (!finish_status.ok()) {
             std::cerr << "   TTS Client: Finish() also failed after config write failure. Status: ("
                       << finish_status.error_code() << ") " << finish_status.error_message() << std::endl;
        }
        stream_.reset();
        context_.reset();
        session_id_.clear();
        stream_active_.store(false);
        return false;
    }

    stream_active_.store(true);
    std::cout << "✅ TTS Client: Stream successfully started and SynthesisConfig sent for session [" << session_id_ << "]." << std::endl;
    return true;
}

// 텍스트 청크 전송 함수
bool TTSClient::SendTextChunk(const std::string& text) {
    if (!IsStreamActive()) { // 내부적으로 뮤텍스 사용
        // std::cerr << "⚠️ TTS Client: SendTextChunk called but stream is not active for session [" << session_id_ << "]." << std::endl; // 로그 줄임
        return false;
    }

    TTSStreamRequest request; // 네임스페이스 불필요 (using 선언됨)
    request.set_text_chunk(text);

    bool write_ok = false;
    {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        if (stream_) {
            write_ok = stream_->Write(request);
        } else {
            write_ok = false;
            // std::cerr << "⚠️ TTS Client: Stream became invalid before writing chunk for session [" << session_id_ << "]." << std::endl; // 로그 줄임
        }
    } // 뮤텍스 락 해제

    if (!write_ok) {
        // std::cerr << "❌ TTS Client: Failed to write text chunk to TTS engine stream for session [" << session_id_ << "]. Marking as inactive." << std::endl; // 로그 줄임
        stream_active_.store(false); // 쓰기 실패 시 비활성 상태로 표시
    }
    return write_ok;
}

// 스트림 종료 함수
Status TTSClient::FinishStream() {
    std::lock_guard<std::mutex> lock(stream_mutex_);

    if (!stream_active_.load() || !stream_) {
         // std::cerr << "⚠️ TTS Client: FinishStream called but stream is not active or already finished for session [" << session_id_ << "]." << std::endl; // 로그 줄임
         return stream_ ? Status(grpc::StatusCode::FAILED_PRECONDITION, "Stream not active")
                        : Status::OK; // 이미 종료된 경우 OK 반환 (또는 다른 상태)
    }

    std::cout << "⏳ TTS Client: Finishing stream for session [" << session_id_ << "]..." << std::endl;

    // 더 이상 메시지 전송 안 함 알림
    bool writes_done_ok = stream_->WritesDone();
    if (!writes_done_ok) {
        std::cerr << "⚠️ TTS Client: WritesDone failed on TTS stream for session [" << session_id_ << "]. Stream might be broken." << std::endl;
    } else {
        // std::cout << "   TTS Client: WritesDone called successfully for session [" << session_id_ << "]." << std::endl; // 로그 줄임
    }

    // 서버로부터 최종 상태 받기 (google.protobuf.Empty 반환)
    std::cout << "   TTS Client: Waiting for final status (Finish) from TTS server for session [" << session_id_ << "]..." << std::endl;
    Status status = stream_->Finish();

    // 리소스 정리
    stream_.reset();
    context_.reset();
    stream_active_.store(false);
    std::string finished_session_id = session_id_;
    session_id_.clear();

    if (status.ok()) {
        std::cout << "✅ TTS Client: Stream finished successfully for session [" << finished_session_id << "]." << std::endl;
    } else {
        std::cerr << "❌ TTS Client: Stream finished with error for session [" << finished_session_id
                  << "]. Status: (" << status.error_code() << "): " << status.error_message() << std::endl;
    }

    return status;
}

// 스트림 활성 상태 확인 함수
bool TTSClient::IsStreamActive() const {
    std::lock_guard<std::mutex> lock(stream_mutex_);
    return stream_active_.load() && (stream_ != nullptr);
}

// SynthesizeSpeech 함수 제거 (proto에 해당 RPC 없음)

} // namespace llm_engine