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
    stub_ = TTSService::NewStub(channel_);
    if (!stub_) {
        throw std::runtime_error("TTSClient: Failed to create TTSService::Stub.");
    }
    std::cout << "TTSClient initialized with real gRPC channel." << std::endl;
}

// 테스트용 Stub을 사용하는 생성자 (단위 테스트 등에서 사용)
TTSClient::TTSClient(std::shared_ptr<TTSService::StubInterface> stub) 
    : stub_(stub) {
    if (!stub_) {
        throw std::runtime_error("TTSClient: Provided gRPC stub is null.");
    }
    std::cout << "TTSClient initialized with provided stub (for testing)." << std::endl;
}

// 스트림 시작 함수
bool TTSClient::StartStream(const tts::SynthesisConfig& config_from_llm) {

    if (stream_active_.load()) {
        std::cerr << "⚠️ TTS Client: StartStream called while another stream [" << session_id_ << "] is already active." << std::endl;
        return false;
    }
    if (config_from_llm.frontend_session_id().empty() || 
        config_from_llm.language_code().empty() || 
        config_from_llm.voice_name().empty()) {
        std::cerr << "❌ TTS Client: StartStream called with empty frontend_session_id, language_code, or voice_name in config." << std::endl;
        return false;
    }
    if (!stub_) {
         std::cerr << "❌ TTS Client: StartStream called but stub_ is null." << std::endl;
         return false;
    }

    session_id_ = config_from_llm.frontend_session_id(); // frontend_session_id를 내부 대표 ID로 사용
    std::cout << "⏳ TTS Client: Starting stream for frontend_session_id [" << session_id_ 
              << "] (TTS internal session: " << config_from_llm.session_id() 
              << ", Lang: " << config_from_llm.language_code() 
              << ", Voice: " << config_from_llm.voice_name() << ")..." << std::endl;

    context_ = std::make_unique<ClientContext>();
    stream_ = stub_->SynthesizeStream(context_.get(), &server_response_);

    if (!stream_) {
        std::cerr << "❌ TTS Client: Failed to initiate gRPC stream to TTS engine for frontend_session_id [" << session_id_ << "]." << std::endl;
        context_.reset();
        session_id_.clear();
        return false;
    }

    tts::TTSStreamRequest initial_tts_request;
    initial_tts_request.mutable_config()->CopyFrom(config_from_llm);

    std::cout << "   TTS Client: Sending initial SynthesisConfig for FE_SID [" << session_id_ << "] (Content: " << initial_tts_request.config().ShortDebugString() << ")" << std::endl;
    if (!stream_->Write(initial_tts_request)) {
        std::cerr << "❌ TTS Client: Failed to write initial SynthesisConfig for FE_SID [" << session_id_ << "]. Finishing stream." << std::endl;
        grpc::Status finish_status = stream_->Finish(); 
        std::cerr << "   TTS Client: Finish() status after config write failure: ("
                  << finish_status.error_code() << ") " << finish_status.error_message() << std::endl;
        stream_.reset();
        context_.reset();
        session_id_.clear();
        stream_active_.store(false);
        return false;
    }

    stream_active_.store(true);
    std::cout << "✅ TTS Client: Stream successfully started and SynthesisConfig sent for frontend_session_id [" << session_id_ << "]." << std::endl;
    return true;
}

// 텍스트 청크 전송 함수
bool TTSClient::SendTextChunk(const std::string& text) {
    if (!IsStreamActive()) { 
        return false;
    }

    tts::TTSStreamRequest request; 
    request.set_text_chunk(text);

    bool write_ok = false;
    {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        if (stream_) {
            write_ok = stream_->Write(request);
        } else {
            write_ok = false;
        }
    } 

    if (!write_ok) {
        stream_active_.store(false); 
    }
    return write_ok;
}

// 스트림 종료 함수
Status TTSClient::FinishStream() {
    std::lock_guard<std::mutex> lock(stream_mutex_);

    if (!stream_active_.load() || !stream_) {
         return stream_ ? Status(grpc::StatusCode::FAILED_PRECONDITION, "Stream not active")
                        : Status::OK; 
    }

    std::cout << "⏳ TTS Client: Finishing stream for frontend_session_id [" << session_id_ << "]..." << std::endl;

    bool writes_done_ok = stream_->WritesDone();
    if (!writes_done_ok) {
        std::cerr << "⚠️ TTS Client: WritesDone failed on TTS stream for FE_SID [" << session_id_ << "]. Stream might be broken." << std::endl;
    } else {
        // std::cout << "   TTS Client: WritesDone called successfully for FE_SID [" << session_id_ << "]." << std::endl; 
    }

    std::cout << "   TTS Client: Waiting for final status (Finish) from TTS server for FE_SID [" << session_id_ << "]..." << std::endl;
    Status status = stream_->Finish();

    stream_.reset();
    context_.reset();
    stream_active_.store(false);
    std::string finished_session_id = session_id_;
    session_id_.clear();

    if (status.ok()) {
        std::cout << "✅ TTS Client: Stream finished successfully for FE_SID [" << finished_session_id << "]." << std::endl;
    } else {
        std::cerr << "❌ TTS Client: Stream finished with error for FE_SID [" << finished_session_id
                  << "]. Status: (" << status.error_code() << "): " << status.error_message() << std::endl;
    }

    return status;
}

bool TTSClient::IsStreamActive() const {
    std::lock_guard<std::mutex> lock(stream_mutex_);
    return stream_active_.load() && (stream_ != nullptr);
}

} // namespace llm_engine
