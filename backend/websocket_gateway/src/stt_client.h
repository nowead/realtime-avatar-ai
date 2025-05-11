#ifndef STT_CLIENT_H
#define STT_CLIENT_H

#include <grpcpp/grpcpp.h>
#include "stt.grpc.pb.h" 
#include <string>
#include <functional>
#include <thread>       
#include <atomic>
#include <memory>       
#include <mutex>      

namespace websocket_gateway { 

class STTClient {
public:
    using StatusCallback = std::function<void(const grpc::Status& status)>;

    explicit STTClient(const std::string& target_address);
    ~STTClient();

    STTClient(const STTClient&) = delete;
    STTClient& operator=(const STTClient&) = delete;
    STTClient(STTClient&&) = delete;
    STTClient& operator=(STTClient&&) = delete;

    bool StartStream(const stt::RecognitionConfig& config, StatusCallback on_finish);
    bool WriteAudioChunk(const std::string& audio_data_chunk); 
    void WritesDoneAndFinish(); 
    void StopStreamNow(); // gRPC 스트림 즉시 중단 시도
    bool IsStreamActive() const; 

private:
    std::string target_address_; 
    std::string frontend_session_id_; 

    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<stt::STTService::Stub> stub_;

    std::unique_ptr<grpc::ClientContext> context_;
    std::unique_ptr<grpc::ClientWriter<stt::STTStreamRequest>> writer_;
    google::protobuf::Empty response_placeholder_; 

    std::atomic<bool> stream_active_{false};
    StatusCallback status_callback_;
    
    std::thread completion_thread_;
    void StreamCompletionTask(); 

    mutable std::mutex stream_mutex_; 
};

} // namespace websocket_gateway

#endif // STT_CLIENT_H
