#ifndef STT_CLIENT_H
#define STT_CLIENT_H

#include <grpcpp/grpcpp.h>
#include "stt.grpc.pb.h" // stt.proto에서 생성된 헤더
#include <string>
#include <functional>
#include <thread>       // std::thread (간단한 예제용, 실제론 비동기 API 권장)
#include <atomic>
#include <memory>       // std::unique_ptr

// stt.proto 에 정의된 메시지 사용
using stt::STTStreamRequest;
using stt::RecognitionConfig;

class STTClient {
public:
    using StatusCallback = std::function<void(const grpc::Status& status)>;

    STTClient(const std::string& target_address);
    ~STTClient();

    // 스트림 시작 시 RecognitionConfig를 보내고, 이후 오디오 청크를 보냄
    bool StartStream(const RecognitionConfig& config, StatusCallback on_finish);
    bool WriteAudioChunk(const std::string& audio_data_chunk); // bytes는 std::string으로 매핑됨
    void WritesDoneAndFinish(); // 오디오 전송 완료 및 스트림 종료 요청

    // 스트림을 즉시 중단하고 싶을 때 사용
    void StopStreamNow();

private:
    std::unique_ptr<stt::STTService::Stub> stub_;
    std::shared_ptr<grpc::Channel> channel_;

    std::unique_ptr<grpc::ClientContext> context_;
    // RecognizeStream은 클라이언트 스트리밍이므로 ClientWriter 사용
    std::unique_ptr<grpc::ClientWriter<STTStreamRequest>> writer_;
    google::protobuf::Empty response_placeholder_; // 응답은 Empty

    std::atomic<bool> stream_active_{false};
    StatusCallback status_callback_;

    // 스트림 완료를 기다리는 스레드 (간단한 예제용)
    // 실제 프로덕션에서는 gRPC 비동기 API (CompletionQueue) 사용 권장
    std::thread completion_thread_;
    void StreamCompletionTask(); // 스레드에서 실행될 함수
};

#endif // STT_CLIENT_H