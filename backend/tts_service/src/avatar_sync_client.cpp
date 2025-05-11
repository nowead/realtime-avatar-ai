#include "avatar_sync_client.h"
#include <iostream>
#include <vector> // For std::vector with SendAudioChunk

namespace tts {

AvatarSyncClient::AvatarSyncClient(const std::string& server_address)
  : server_address_(server_address) {
    try {
        channel_ = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
        if (!channel_) {
            throw std::runtime_error("Failed to create gRPC channel to AvatarSync service: " + server_address);
        }
        stub_ = AvatarSyncService::NewStub(channel_);
        if (!stub_) {
             throw std::runtime_error("Failed to create AvatarSyncService::Stub.");
        }
    } catch (const std::exception& e) {
        std::cerr << "❌ Exception in AvatarSyncClient constructor: " << e.what() << std::endl;
        throw;
    }
    std::cout << "  AvatarSyncClient initialized for address: " << server_address << std::endl;
}

AvatarSyncClient::~AvatarSyncClient() {
    std::cout << "ℹ️ Destroying AvatarSyncClient for FE_SID [" << current_frontend_session_id_ << "]..." << std::endl;
    if (IsStreamActive()) {
        std::cerr << "⚠️ WARNING: AvatarSyncClient destroyed while stream was active for FE_SID ["
                  << current_frontend_session_id_ << "]. Attempting to finish stream..." << std::endl;
        try {
            FinishStream(); // 반환값(Status)은 무시
        } catch(const std::exception& e) {
            std::cerr << "   Exception during cleanup in destructor: " << e.what() << std::endl;
        }
    }
     stub_.reset();
     channel_.reset();
    std::cout << "✅ AvatarSyncClient destroyed for FE_SID [" << current_frontend_session_id_ << "]." << std::endl;
}

// ★ 함수 시그니처를 const avatar_sync::SyncConfig& config 로 수정
bool AvatarSyncClient::StartStream(const avatar_sync::SyncConfig& config_from_tts) {
    std::lock_guard<std::mutex> lock(stream_mutex_);

    if (stream_active_.load()) {
        std::cerr << "⚠️ AvatarSyncClient: StartStream called while another stream (FE_SID ["
                  << current_frontend_session_id_ << "]) is already active. Finish the previous stream first." << std::endl;
        return false;
    }
    // SyncConfig에 frontend_session_id 필드가 있는지, 그리고 비어있지 않은지 확인 (proto 수정 사항 반영)
    if (config_from_tts.frontend_session_id().empty()) {
        std::cerr << "❌ AvatarSyncClient: StartStream called with empty frontend_session_id in SyncConfig." << std::endl;
        return false;
    }

    current_frontend_session_id_ = config_from_tts.frontend_session_id(); // 프론트엔드 세션 ID 저장
    std::cout << "⏳ AvatarSyncClient: Starting stream for frontend_session_id [" << current_frontend_session_id_ << "]..." << std::endl;

    context_ = std::make_unique<ClientContext>();
    stream_ = stub_->SyncAvatarStream(context_.get(), &server_response_);

    if (!stream_) {
        std::cerr << "❌ AvatarSyncClient: Failed to initiate gRPC stream to AvatarSync service for FE_SID [" << current_frontend_session_id_ << "]." << std::endl;
        context_.reset(); 
        current_frontend_session_id_.clear(); 
        return false;
    }

    // 스트림 시작 직후 SyncConfig 메시지 전송
    AvatarSyncStreamRequest config_request;
    // 전달받은 SyncConfig 객체를 그대로 AvatarSyncStreamRequest의 config 필드에 설정
    config_request.mutable_config()->CopyFrom(config_from_tts); // ★ frontend_session_id가 포함된 config 전달

    std::cout << "   AvatarSyncClient: Sending initial SyncConfig for FE_SID [" << current_frontend_session_id_ << "] (Content: " << config_request.config().ShortDebugString() << ")" << std::endl;
    if (!stream_->Write(config_request)) {
        std::cerr << "❌ AvatarSyncClient: Failed to write initial SyncConfig for FE_SID [" << current_frontend_session_id_ << "]. Finishing stream." << std::endl;
        Status finish_status = stream_->Finish(); 
        std::cerr << "   AvatarSyncClient: Finish() status after config write failure: (" << finish_status.error_code() << ") " << finish_status.error_message() << std::endl;
        stream_.reset();
        context_.reset();
        current_frontend_session_id_.clear();
        stream_active_.store(false); 
        return false;
    }

    stream_active_.store(true);
    std::cout << "✅ AvatarSyncClient: Stream successfully started and SyncConfig sent for FE_SID [" << current_frontend_session_id_ << "]." << std::endl;
    return true;
}

bool AvatarSyncClient::SendAudioChunk(const std::vector<uint8_t>& audio_chunk) {
    if (!IsStreamActive()) { // IsStreamActive는 내부적으로 stream_mutex_ 사용
        // std::cerr << "⚠️ AvatarSyncClient: SendAudioChunk called but stream is not active for FE_SID [" << current_frontend_session_id_ << "]." << std::endl; // 로그가 너무 많을 수 있어 주석 처리
        return false;
    }
    if (audio_chunk.empty()) {
        return true; 
    }

    AvatarSyncStreamRequest request;
    request.set_audio_chunk(audio_chunk.data(), audio_chunk.size());

    bool write_ok = false;
    {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        if (stream_) { 
            // std::cout << "   AvatarSyncClient: Sending audio chunk (" << audio_chunk.size() << " bytes) for FE_SID [" << current_frontend_session_id_ << "]" << std::endl; // 로그가 너무 많을 수 있어 주석 처리
            write_ok = stream_->Write(request);
        }
    }

    if (!write_ok) {
        std::cerr << "❌ AvatarSyncClient: Failed to write audio chunk to stream for FE_SID [" << current_frontend_session_id_
                  << "]. Marking as inactive." << std::endl;
        stream_active_.store(false); 
        return false;
    }
    return true;
}

bool AvatarSyncClient::SendVisemeData(const VisemeData& viseme_data) {
    if (!IsStreamActive()) {
        // std::cerr << "⚠️ AvatarSyncClient: SendVisemeData called but stream is not active for FE_SID [" << current_frontend_session_id_ << "]." << std::endl;
        return false;
    }

    AvatarSyncStreamRequest request;
    *request.mutable_viseme_data() = viseme_data;

    bool write_ok = false;
    {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        if (stream_) {
            // std::cout << "   AvatarSyncClient: Sending viseme data (ID: " << viseme_data.viseme_id() << ") for FE_SID [" << current_frontend_session_id_ << "]" << std::endl;
            write_ok = stream_->Write(request);
        }
    }

    if (!write_ok) {
        std::cerr << "❌ AvatarSyncClient: Failed to write viseme data to stream for FE_SID [" << current_frontend_session_id_
                  << "]. Marking as inactive." << std::endl;
        stream_active_.store(false);
        return false;
    }
    return true;
}

bool AvatarSyncClient::SendVisemeDataBatch(const std::vector<VisemeData>& visemes) {
    if (!IsStreamActive()) {
        // std::cerr << "⚠️ AvatarSyncClient: SendVisemeDataBatch called but stream is not active for FE_SID [" << current_frontend_session_id_ << "]." << std::endl;
        return false;
    }
    if (visemes.empty()) {
        return true;
    }

    bool all_writes_ok = true;
    for (const auto& viseme : visemes) {
        if (!SendVisemeData(viseme)) { 
            all_writes_ok = false;
            break;
        }
    }
    return all_writes_ok;
}


Status AvatarSyncClient::FinishStream() {
    std::lock_guard<std::mutex> lock(stream_mutex_);

    if (!stream_active_.load() || !stream_) {
         std::cerr << "⚠️ AvatarSyncClient: FinishStream called but stream is not active or already finished for FE_SID [" << current_frontend_session_id_ << "]." << std::endl;
         return Status(grpc::StatusCode::FAILED_PRECONDITION, "Stream not active or already finished");
    }

    std::cout << "⏳ AvatarSyncClient: Finishing stream for FE_SID [" << current_frontend_session_id_ << "]..." << std::endl;

    bool writes_done_ok = stream_->WritesDone();
    if (!writes_done_ok) {
         std::cerr << "⚠️ AvatarSyncClient: WritesDone failed on stream for FE_SID [" << current_frontend_session_id_
                   << "] (stream might already be broken)." << std::endl;
    } else {
         std::cout << "   AvatarSyncClient: WritesDone called successfully for FE_SID [" << current_frontend_session_id_ << "]." << std::endl;
    }

    Status status = stream_->Finish();

    stream_.reset();
    context_.reset();
    stream_active_.store(false);
    std::string finished_session_id = current_frontend_session_id_; 
    current_frontend_session_id_.clear(); 

    if (status.ok()) {
        std::cout << "✅ AvatarSyncClient: Stream finished successfully for FE_SID [" << finished_session_id << "]. Server returned Empty." << std::endl;
    } else {
        std::cerr << "❌ AvatarSyncClient: Stream finished with error for FE_SID [" << finished_session_id
                  << "]. Status: (" << status.error_code() << "): " << status.error_message() << std::endl;
    }
    return status;
}

bool AvatarSyncClient::IsStreamActive() const {
    std::lock_guard<std::mutex> lock(stream_mutex_); 
    return stream_active_.load() && (stream_ != nullptr); 
}

} // namespace tts
