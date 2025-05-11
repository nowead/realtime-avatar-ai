#include "stt_client.h"
#include <iostream>
#include <cstdlib> // std::getenv
#include "google/protobuf/empty.pb.h" // Empty 메시지 사용

STTClient::STTClient(const std::string& target_address) {
    grpc::ChannelArguments args;
    // 필요한 경우 채널 인자 설정
    // args.SetMaxReceiveMessageSize(-1); // 예시
    channel_ = grpc::CreateCustomChannel(target_address, grpc::InsecureChannelCredentials(), args);
    stub_ = stt::STTService::NewStub(channel_);
    std::cout << "STTClient created for target: " << target_address << std::endl;
}

STTClient::~STTClient() {
    // 진행 중인 스트림이 있다면 정리 시도
    if (stream_active_.load()) {
        StopStreamNow(); // 컨텍스트 취소 시도
    }
    if (completion_thread_.joinable()) {
        completion_thread_.join(); // 완료 대기 스레드 확실히 종료
    }
    std::cout << "STTClient destroyed." << std::endl;
}

bool STTClient::StartStream(const RecognitionConfig& config, StatusCallback on_finish) {
    if (stream_active_.exchange(true)) { // 이미 활성화 상태였다면 true 반환
        std::cerr << "STTClient: Stream already active." << std::endl;
        if (on_finish) {
            on_finish(grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "Stream already active"));
        }
        stream_active_ = true; // 원상복구 (실패했으므로)
        return false;
    }

    context_ = std::make_unique<grpc::ClientContext>();
    // 필요 시 메타데이터 추가 (예: 세션 ID를 메타데이터로도 전달)
    // context_->AddMetadata("session-id", config.session_id());

    writer_ = stub_->RecognizeStream(context_.get(), &response_placeholder_);

    if (!writer_) {
        std::cerr << "STTClient: Failed to start stream (stub_->RecognizeStream returned nullptr)." << std::endl;
        if (on_finish) {
            on_finish(grpc::Status(grpc::StatusCode::INTERNAL, "Failed to initialize stream"));
        }
        stream_active_ = false; // 실패 시 다시 false로
        return false;
    }
    // stream_active_는 이미 true로 설정됨
    status_callback_ = std::move(on_finish);

    // 첫 메시지로 RecognitionConfig 전송
    STTStreamRequest init_request;
    init_request.mutable_config()->CopyFrom(config);
    if (!writer_->Write(init_request)) {
        std::cerr << "STTClient: Failed to write initial RecognitionConfig." << std::endl;
        grpc::Status status = writer_->Finish(); // 스트림 정리 시도
        if (status_callback_) {
            status_callback_(status);
        }
        stream_active_ = false;
        return false;
    }

    std::cout << "STTClient: Stream started and RecognitionConfig sent for session_id: " << config.session_id() << std::endl;
    return true;
}

bool STTClient::WriteAudioChunk(const std::string& audio_data_chunk) {
    if (!stream_active_.load() || !writer_) {
        std::cerr << "STTClient: Stream not active or not initialized for writing audio." << std::endl;
        return false;
    }
    STTStreamRequest request;
    request.set_audio_chunk(audio_data_chunk);
    if (writer_->Write(request)) {
        return true;
    }
    // 쓰기 실패는 심각한 오류일 수 있음. 스트림 상태 확인 및 정리 필요.
    std::cerr << "STTClient: Failed to write audio chunk. Stream might be broken." << std::endl;
    // 이 경우, WritesDoneAndFinish를 호출하여 상태를 확인하고 콜백을 실행하는 것이 좋음
    // WritesDoneAndFinish(); // 또는 별도의 오류 처리 로직
    return false;
}

// 이 함수는 호출 즉시 반환되며, 실제 작업은 백그라운드 스레드에서 수행됩니다.
void STTClient::WritesDoneAndFinish() {
    if (!stream_active_.load() || !writer_) {
        std::cerr << "STTClient: Stream not active or not initialized for WritesDoneAndFinish." << std::endl;
        if(status_callback_ && !stream_active_.load()) { // 스트림이 시작되지도 않았다면
             status_callback_(grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "Stream not active or writer not initialized"));
        }
        return;
    }

    // 기존 완료 스레드가 있다면 먼저 join (이전 호출이 아직 안 끝났을 경우 대비)
    if (completion_thread_.joinable()) {
        completion_thread_.join();
    }
    
    std::cout << "STTClient: Scheduling WritesDone and Finish." << std::endl;
    // 실제 WritesDone과 Finish는 별도 스레드에서 처리하여 현재 스레드 블로킹 방지
    completion_thread_ = std::thread(&STTClient::StreamCompletionTask, this);
}

void STTClient::StreamCompletionTask() {
    if (!writer_) { // writer_가 null이면 이미 문제가 발생한 상태
        if (status_callback_) {
             status_callback_(grpc::Status(grpc::StatusCode::INTERNAL, "Writer is null in completion task."));
        }
        stream_active_ = false;
        return;
    }

    bool ws_done = writer_->WritesDone(); // 클라이언트 측에서 모든 메시지 전송 완료
    if (!ws_done) {
        std::cerr << "STTClient: WritesDone failed on client side. Stream might be broken." << std::endl;
        // WritesDone이 false를 반환하면 이미 스트림에 문제가 발생했을 가능성이 높음
    }

    grpc::Status status = writer_->Finish(); // 서버로부터 Empty 응답과 최종 상태를 받음
    stream_active_ = false; // 스트림 종료

    if (status_callback_) {
        status_callback_(status);
    }
    std::cout << "STTClient: Stream completion task finished with status: (" << status.error_code() << ") " << status.error_message() << std::endl;
}


void STTClient::StopStreamNow() {
    // 이 함수는 스트림을 즉시 중단하려고 시도합니다.
    // 이미 WritesDoneAndFinish가 호출되어 completion_thread_가 실행 중일 수 있습니다.
    // context_ 취소는 진행 중인 gRPC 호출을 중단시킵니다.
    if (context_) {
        context_->TryCancel();
    }
    stream_active_ = false; // 콜백 등에서 더 이상 작업하지 않도록 플래그 설정
    std::cout << "STTClient: Stream stop requested (TryCancel)." << std::endl;
    // completion_thread_가 있다면, 취소로 인해 빠르게 종료될 것입니다.
    // join은 소멸자나 다른 정리 시점에서 호출될 수 있습니다.
}