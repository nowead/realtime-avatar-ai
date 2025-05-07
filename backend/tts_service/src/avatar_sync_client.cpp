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
    std::cout << "ℹ️ Destroying AvatarSyncClient..." << std::endl;
    if (IsStreamActive()) {
        std::cerr << "⚠️ WARNING: AvatarSyncClient destroyed while stream was active for session ["
                  << session_id_ << "]. Attempting to finish stream..." << std::endl;
        try {
            FinishStream(); // 반환값(Status)은 무시
        } catch(const std::exception& e) {
            std::cerr << "   Exception during cleanup in destructor: " << e.what() << std::endl;
        }
    }
     stub_.reset();
     channel_.reset();
    std::cout << "✅ AvatarSyncClient destroyed." << std::endl;
}

bool AvatarSyncClient::StartStream(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(stream_mutex_);

    if (stream_active_.load()) {
        std::cerr << "⚠️ AvatarSyncClient: StartStream called while another stream (session ["
                  << session_id_ << "]) is already active. Finish the previous stream first." << std::endl;
        return false;
    }
    if (session_id.empty()) {
        std::cerr << "❌ AvatarSyncClient: StartStream called with empty session_id." << std::endl;
        return false;
    }

    session_id_ = session_id;
    std::cout << "⏳ AvatarSyncClient: Starting stream for session [" << session_id_ << "]..." << std::endl;

    context_ = std::make_unique<ClientContext>();
    // server_response_는 Empty 타입이므로 Clear() 불필요
    stream_ = stub_->SyncAvatarStream(context_.get(), &server_response_);

    if (!stream_) {
        std::cerr << "❌ AvatarSyncClient: Failed to initiate gRPC stream to AvatarSync service for session [" << session_id_ << "]." << std::endl;
        context_.reset(); // 컨텍스트 정리
        session_id_.clear(); // 세션 ID 정리
        return false;
    }

    // 스트림 시작 직후 SyncConfig 메시지 전송
    AvatarSyncStreamRequest config_request;
    SyncConfig* config_msg = config_request.mutable_config(); // oneof 필드 접근
    config_msg->set_session_id(session_id_);
    // 필요시 추가 설정: config_msg->set_avatar_model_id("default_model");

    if (!stream_->Write(config_request)) {
        std::cerr << "❌ AvatarSyncClient: Failed to write initial SyncConfig for session [" << session_id_ << "]. Finishing stream." << std::endl;
        stream_->Finish(); // 상태는 무시하고 종료만 시도
        stream_.reset();
        context_.reset();
        session_id_.clear();
        stream_active_.store(false); // 확실히 비활성 상태로
        return false;
    }

    stream_active_.store(true);
    std::cout << "✅ AvatarSyncClient: Stream successfully started and SyncConfig sent for session [" << session_id_ << "]." << std::endl;
    return true;
}

bool AvatarSyncClient::SendAudioChunk(const std::vector<uint8_t>& audio_chunk) {
    if (!IsStreamActive()) {
        std::cerr << "⚠️ AvatarSyncClient: SendAudioChunk called but stream is not active for session [" << session_id_ << "]." << std::endl;
        return false;
    }
    if (audio_chunk.empty()) {
        // std::cout << "   AvatarSyncClient: Received empty audio chunk for session [" << session_id_ << "], skipping." << std::endl;
        return true; // 빈 청크는 무시하고 성공으로 간주
    }

    AvatarSyncStreamRequest request;
    // bytes 필드에 직접 데이터 설정 (std::vector<uint8_t> -> const void*, size_t)
    request.set_audio_chunk(audio_chunk.data(), audio_chunk.size());

    bool write_ok = false;
    {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        if (stream_) { // 스트림이 유효한지 다시 한번 확인
            // std::cout << "   AvatarSyncClient: Sending audio chunk (" << audio_chunk.size() << " bytes) for session [" << session_id_ << "]" << std::endl;
            write_ok = stream_->Write(request);
        }
    }

    if (!write_ok) {
        std::cerr << "❌ AvatarSyncClient: Failed to write audio chunk to stream for session [" << session_id_
                  << "]. Marking as inactive." << std::endl;
        stream_active_.store(false); // 쓰기 실패 시 비활성 처리
        return false;
    }
    return true;
}

bool AvatarSyncClient::SendVisemeData(const VisemeData& viseme_data) {
    if (!IsStreamActive()) {
        std::cerr << "⚠️ AvatarSyncClient: SendVisemeData called but stream is not active for session [" << session_id_ << "]." << std::endl;
        return false;
    }

    AvatarSyncStreamRequest request;
    // VisemeData 메시지 복사
    *request.mutable_viseme_data() = viseme_data;

    bool write_ok = false;
    {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        if (stream_) {
            // std::cout << "   AvatarSyncClient: Sending viseme data (ID: " << viseme_data.viseme_id() << ") for session [" << session_id_ << "]" << std::endl;
            write_ok = stream_->Write(request);
        }
    }

    if (!write_ok) {
        std::cerr << "❌ AvatarSyncClient: Failed to write viseme data to stream for session [" << session_id_
                  << "]. Marking as inactive." << std::endl;
        stream_active_.store(false);
        return false;
    }
    return true;
}

bool AvatarSyncClient::SendVisemeDataBatch(const std::vector<VisemeData>& visemes) {
    if (!IsStreamActive()) {
        std::cerr << "⚠️ AvatarSyncClient: SendVisemeDataBatch called but stream is not active for session [" << session_id_ << "]." << std::endl;
        return false;
    }
    if (visemes.empty()) {
        return true;
    }

    bool all_writes_ok = true;
    for (const auto& viseme : visemes) {
        if (!SendVisemeData(viseme)) { // 내부적으로 뮤텍스 사용
            all_writes_ok = false;
            // 스트림이 이미 비활성화되었을 수 있으므로 루프 중단
            break;
        }
    }
    return all_writes_ok;
}


Status AvatarSyncClient::FinishStream() {
    std::lock_guard<std::mutex> lock(stream_mutex_);

    if (!stream_active_.load() || !stream_) {
         std::cerr << "⚠️ AvatarSyncClient: FinishStream called but stream is not active or already finished for session [" << session_id_ << "]." << std::endl;
         return Status(grpc::StatusCode::FAILED_PRECONDITION, "Stream not active or already finished");
    }

    std::cout << "⏳ AvatarSyncClient: Finishing stream for session [" << session_id_ << "]..." << std::endl;

    bool writes_done_ok = stream_->WritesDone();
    if (!writes_done_ok) {
         // WritesDone 실패는 스트림이 이미 깨졌을 가능성을 시사. 로깅만 하고 진행.
         std::cerr << "⚠️ AvatarSyncClient: WritesDone failed on stream for session [" << session_id_
                   << "] (stream might already be broken)." << std::endl;
    } else {
         std::cout << "   AvatarSyncClient: WritesDone called successfully for session [" << session_id_ << "]." << std::endl;
    }

    // 서버로부터 최종 상태(Empty 응답) 수신
    Status status = stream_->Finish();

    // 리소스 정리
    stream_.reset();
    context_.reset();
    stream_active_.store(false);
    std::string current_session_id = session_id_; // 로그용으로 현재 세션 ID 저장
    session_id_.clear(); // 세션 ID 초기화

    if (status.ok()) {
        std::cout << "✅ AvatarSyncClient: Stream finished successfully for session [" << current_session_id << "]. Server returned Empty." << std::endl;
    } else {
        std::cerr << "❌ AvatarSyncClient: Stream finished with error for session [" << current_session_id
                  << "]. Status: (" << status.error_code() << "): " << status.error_message() << std::endl;
    }
    return status;
}

bool AvatarSyncClient::IsStreamActive() const {
    std::lock_guard<std::mutex> lock(stream_mutex_); // const 함수에서도 뮤텍스 사용 가능 (mutable)
    return stream_active_.load() && (stream_ != nullptr); // 스트림 객체 유효성도 함께 확인
}

} // namespace tts