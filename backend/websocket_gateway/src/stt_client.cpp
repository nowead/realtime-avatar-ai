#include "stt_client.h" 
#include <iostream>
#include <cstdlib> 
#include <stdexcept> 
#include "google/protobuf/empty.pb.h" 
#include "stt.pb.h" 
#include <string_view> 

namespace websocket_gateway { 

inline std::string svToString(std::string_view sv) {
    if (sv.data() == nullptr) return "";
    return std::string(sv.data(), sv.length());
}
inline std::string svToString(const grpc::string_ref& sr) {
    if (sr.data() == nullptr) return "";
    return std::string(sr.data(), sr.length());
}
inline std::string svToString(const std::string& s) {
    return s;
}

STTClient::STTClient(const std::string& target_address) : target_address_(target_address) {
    grpc::ChannelArguments args;
    try {
        channel_ = grpc::CreateCustomChannel(target_address_, grpc::InsecureChannelCredentials(), args);
        if (!channel_) { 
            throw std::runtime_error("Failed to create gRPC channel for STTClient to " + target_address_);
        }
        stub_ = stt::STTService::NewStub(channel_);
        if (!stub_) { 
            throw std::runtime_error("Failed to create STTService::Stub for STTClient.");
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception in STTClient constructor: " << e.what() << std::endl;
        throw; 
    }
    std::cout << "STTClient created for target: " << target_address_ << std::endl;
}

STTClient::~STTClient() {
    std::cout << "STTClient destructor called for FE_SID [" << frontend_session_id_ << "]." << std::endl;
    if (stream_active_.load()) {
        std::cout << "   STTClient: Stream was active during destruction. Attempting to stop (TryCancel) for FE_SID [" << frontend_session_id_ << "]." << std::endl;
        StopStreamNow(); 
    }
    if (completion_thread_.joinable()) {
        std::cout << "   STTClient: Joining completion thread for FE_SID [" << frontend_session_id_ << "] in destructor..." << std::endl;
        try {
            completion_thread_.join(); 
            std::cout << "   STTClient: Completion thread joined successfully for FE_SID [" << frontend_session_id_ << "]." << std::endl;
        } catch (const std::system_error& e) {
            std::cerr << "   STTClient: Exception caught while joining completion thread for FE_SID [" << frontend_session_id_ << "]: " << e.what() << std::endl;
        }
    }
    std::cout << "STTClient destroyed for FE_SID [" << frontend_session_id_ << "]." << std::endl;
}

bool STTClient::StartStream(const stt::RecognitionConfig& config, StatusCallback on_finish) {
    std::lock_guard<std::mutex> lock(stream_mutex_); 

    bool expected_stream_active = false;
    if (!stream_active_.compare_exchange_strong(expected_stream_active, true)) {
        std::cerr << "STTClient: Stream already active for FE_SID [" << config.frontend_session_id() 
                  << "]. Current FE_SID in client: [" << frontend_session_id_ << "]." << std::endl;
        if (on_finish) {
            on_finish(grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "Stream already active with FE_SID: " + frontend_session_id_));
        }
        stream_active_.store(true); 
        return false; 
    }

    frontend_session_id_ = config.frontend_session_id(); 
    if (frontend_session_id_.empty()){
        std::cerr << "STTClient: CRITICAL - frontend_session_id is empty in RecognitionConfig. Cannot start stream." << std::endl;
        stream_active_.store(false); 
        if(on_finish) {
            on_finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "frontend_session_id cannot be empty"));
        }
        return false;
    }

    if (completion_thread_.joinable()) {
        std::cout << "STTClient: [" << frontend_session_id_ << "] Previous completion thread is joinable. Joining before starting new stream..." << std::endl;
        completion_thread_.join();
        std::cout << "STTClient: [" << frontend_session_id_ << "] Previous completion thread joined." << std::endl;
    }

    context_ = std::make_unique<grpc::ClientContext>();
    std::cout << "STTClient: [" << frontend_session_id_ << "] Attempting to start gRPC stream to STTService." << std::endl;
    writer_ = stub_->RecognizeStream(context_.get(), &response_placeholder_);

    if (!writer_) {
        std::cerr << "STTClient: [" << frontend_session_id_ << "] ❌ FAILED to start gRPC stream (stub_->RecognizeStream returned nullptr)." << std::endl;
        if (on_finish) {
            on_finish(grpc::Status(grpc::StatusCode::INTERNAL, "Failed to initialize stream with STTService (writer is null)"));
        }
        stream_active_.store(false); 
        frontend_session_id_.clear(); 
        context_.reset(); 
        return false;
    }
    
    status_callback_ = std::move(on_finish);

    stt::STTStreamRequest init_request;
    init_request.mutable_config()->CopyFrom(config); 

    std::cout << "STTClient: [" << frontend_session_id_ << "] Sending RecognitionConfig to STTService: " << init_request.ShortDebugString() << std::endl; 

    if (!writer_->Write(init_request)) {
        std::cerr << "STTClient: [" << frontend_session_id_ << "] ❌ FAILED to write initial RecognitionConfig to gRPC stream." << std::endl;
        grpc::Status status = writer_->Finish(); 
        if (status_callback_) {
            status_callback_(status); 
        }
        stream_active_.store(false);
        frontend_session_id_.clear();
        writer_.reset(); 
        context_.reset(); 
        return false;
    }

    std::cout << "STTClient: [" << frontend_session_id_ << "] ✅ Successfully started stream and sent RecognitionConfig." << std::endl;
    return true;
}

bool STTClient::WriteAudioChunk(const std::string& audio_data_chunk) {
    if (!stream_active_.load()) { 
        return false;
    }

    std::lock_guard<std::mutex> lock(stream_mutex_); 
    if (!stream_active_.load() || !writer_) { 
        return false;
    }

    stt::STTStreamRequest request;
    request.set_audio_chunk(audio_data_chunk);
    
    if (writer_->Write(request)) {
        return true;
    }
    
    // grpc::ClientContext에는 IsCancelled() 멤버가 없습니다.
    // 취소 여부는 스트림 작업의 반환값이나 Finish()의 상태 코드로 확인해야 합니다.
    // 여기서는 단순히 context_가 null인지 여부만 로깅합니다.
    std::cerr << "STTClient: [" << frontend_session_id_ << "] ❌ FAILED to write audio chunk. Stream might be broken. Context is "
              << (context_ ? "not null" : "null") << std::endl;
    
    return false;
}

void STTClient::WritesDoneAndFinish() {
    std::unique_lock<std::mutex> lock(stream_mutex_); 
    if (!stream_active_.load()) { 
        std::cout << "STTClient: [" << frontend_session_id_ << "] WritesDoneAndFinish called but stream is not active. No action." << std::endl;
        return; 
    }
    
    if (completion_thread_.joinable()) {
        lock.unlock(); 
        std::cout << "STTClient: [" << frontend_session_id_ << "] Previous completion thread is joinable. Joining before starting new one..." << std::endl;
        completion_thread_.join();
        std::cout << "STTClient: [" << frontend_session_id_ << "] Previous completion thread joined." << std::endl;
        lock.lock(); 
    }
    
    if (!writer_) {
        std::cerr << "STTClient: [" << frontend_session_id_ << "] WritesDoneAndFinish: writer_ is null. Cannot start completion task." << std::endl;
        stream_active_.store(false); 
        if(status_callback_) {
            status_callback_(grpc::Status(grpc::StatusCode::INTERNAL, "Writer became null before WritesDoneAndFinish."));
        }
        return;
    }

    std::cout << "STTClient: [" << frontend_session_id_ << "] Scheduling WritesDone and Finish in a new thread." << std::endl;
    completion_thread_ = std::thread(&STTClient::StreamCompletionTask, this);
}

void STTClient::StreamCompletionTask() {
    std::string current_fe_sid;
    StatusCallback local_status_callback;
    std::unique_ptr<grpc::ClientWriter<stt::STTStreamRequest>> local_writer; 
    std::unique_ptr<grpc::ClientContext> local_context; 

    { 
        std::lock_guard<std::mutex> lock(stream_mutex_);
        if (!writer_ || !context_) { 
             std::cerr << "STTClient: [" << (frontend_session_id_.empty() ? "NO_FE_SID" : frontend_session_id_) 
                       << "] Writer or Context is null in StreamCompletionTask before moving. Cannot proceed." << std::endl;
             if (status_callback_) {
                 status_callback_(grpc::Status(grpc::StatusCode::INTERNAL, "Writer or Context is null in completion task. FE_SID [" + frontend_session_id_ + "]"));
             }
             stream_active_.store(false); 
             writer_.reset(); 
             context_.reset();
             return;
        }
        current_fe_sid = frontend_session_id_; 
        local_status_callback = status_callback_;
        local_writer = std::move(writer_); 
        local_context = std::move(context_); 
    }

    std::cout << "STTClient: [" << current_fe_sid << "] StreamCompletionTask started." << std::endl;

    bool ws_done_success = false;
    grpc::Status status = grpc::Status::OK;
    
    if (local_writer) { 
        // local_context->IsCancelled() 호출 제거
        // 취소 여부는 Finish()의 상태 코드로 판단합니다.
        ws_done_success = local_writer->WritesDone(); 
        if (!ws_done_success) {
            std::cerr << "STTClient: [" << current_fe_sid << "] WritesDone failed on client side. Stream might be broken." << std::endl;
            // WritesDone 실패 시에도 Finish()는 호출하여 서버로부터 상태를 받아야 할 수 있습니다.
        } else {
            std::cout << "STTClient: [" << current_fe_sid << "] WritesDone successful." << std::endl;
        }

        try {
            status = local_writer->Finish(); 
        } catch (const std::exception& e) {
            std::cerr << "STTClient: [" << current_fe_sid << "] Exception during local_writer->Finish(): " << e.what() << std::endl;
            status = grpc::Status(grpc::StatusCode::INTERNAL, "Exception during writer->Finish(): " + std::string(e.what()));
        } catch (...) {
            std::cerr << "STTClient: [" << current_fe_sid << "] Unknown exception during local_writer->Finish()." << std::endl;
            status = grpc::Status(grpc::StatusCode::UNKNOWN, "Unknown exception during writer->Finish()");
        }
    } else { 
         std::cerr << "STTClient: [" << current_fe_sid << "] CRITICAL: local_writer is null in completion task after move." << std::endl;
         status = grpc::Status(grpc::StatusCode::INTERNAL, "local_writer became null unexpectedly in completion task.");
    }
    
    {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        stream_active_.store(false); 

        if (local_status_callback) { 
            try {
                local_status_callback(status); 
            } catch (const std::exception& e) {
                std::cerr << "STTClient: [" << current_fe_sid << "] Exception in status_callback_: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "STTClient: [" << current_fe_sid << "] Unknown exception in status_callback_." << std::endl;
            }
        }
    }
    std::cout << "STTClient: [" << current_fe_sid << "] Stream completion task finished with status: (" 
              << status.error_code() << ") " << svToString(status.error_message()) << std::endl;
}

void STTClient::StopStreamNow() {
    std::unique_lock<std::mutex> lock(stream_mutex_); 
    std::string current_fe_sid = frontend_session_id_; 
    
    if (!stream_active_.load() && !context_ && !writer_) {
        return;
    }

    std::cout << "STTClient: [" << current_fe_sid << "] StopStreamNow requested. Current active state: " << stream_active_.load() << std::endl;

    if (context_) {
        std::cout << "STTClient: [" << current_fe_sid << "] Attempting to cancel gRPC context (TryCancel)." << std::endl;
        context_->TryCancel(); 
    }
    
    if (stream_active_.exchange(false)) { 
        std::cout << "STTClient: [" << current_fe_sid << "] Stream marked as inactive due to StopStreamNow." << std::endl;
        writer_.reset(); // Writer도 정리
    }
    std::cout << "STTClient: [" << current_fe_sid << "] StopStreamNow processing finished." << std::endl;
}

bool STTClient::IsStreamActive() const {
    return stream_active_.load();
}

} // namespace websocket_gateway
