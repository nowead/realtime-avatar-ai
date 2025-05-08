#include "avatar_sync_service.h" // 헤더 파일 이름 일치
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <vector>

namespace avatar_sync_service {

AvatarSyncServiceImpl::AvatarSyncServiceImpl(std::shared_ptr<WebRTCHandler> handler)
    : webrtc_handler_(handler) {
    if (!webrtc_handler_) {
        throw std::runtime_error("WebRTCHandler cannot be null");
    }
}

grpc::Status AvatarSyncServiceImpl::SyncAvatarStream(
    grpc::ServerContext* context,
    grpc::ServerReader<avatar_sync::AvatarSyncStreamRequest>* reader,
    google::protobuf::Empty* /*response*/) { // response는 사용하지 않으므로 주석 처리 또는 변수명 생략

    avatar_sync::AvatarSyncStreamRequest request;
    std::string current_session_id;
    bool config_received = false;

    std::cout << "Client connected for SyncAvatarStream." << std::endl;

    // 클라이언트 연결 종료 시 WebRTC 세션 정리
    context->AsyncNotifyWhenDone(static_cast<void*>(&current_session_id)); // 태그로 session_id 주소 사용

    while (reader->Read(&request)) {
        if (context->IsCancelled()) {
            std::cout << "Context cancelled by client." << std::endl;
            break;
        }

        switch (request.request_data_case()) {
            case avatar_sync::AvatarSyncStreamRequest::kConfig: {
                const auto& config = request.config();
                current_session_id = config.session_id();
                config_received = true;
                std::cout << "Received SyncConfig for session_id: " << current_session_id << std::endl;
                // WebRTCHandler에 세션 준비 요청 (데이터 채널 생성 등)
                webrtc_handler_->GetDataChannelForSession(current_session_id);
                break;
            }
            case avatar_sync::AvatarSyncStreamRequest::kAudioChunk: {
                if (!config_received) {
                    std::cerr << "Audio chunk received before SyncConfig. Ignoring." << std::endl;
                    continue;
                }
                const auto& audio_chunk_bytes = request.audio_chunk();
                std::vector<uint8_t> audio_data(audio_chunk_bytes.begin(), audio_chunk_bytes.end());
                // std::cout << "Received audio chunk for session " << current_session_id << ", size: " << audio_data.size() << std::endl;
                webrtc_handler_->SendAudioData(current_session_id, audio_data);
                break;
            }
            case avatar_sync::AvatarSyncStreamRequest::kVisemeData: {
                if (!config_received) {
                    std::cerr << "Viseme data received before SyncConfig. Ignoring." << std::endl;
                    continue;
                }
                const auto& viseme = request.viseme_data();
                // google::protobuf::Timestamp를 초 단위로 변환
                double start_time_sec = static_cast<double>(viseme.start_time().seconds()) +
                                        static_cast<double>(viseme.start_time().nanos()) / 1e9;

                // std::cout << "Received VisemeData for session " << current_session_id
                //           << ": ID=" << viseme.viseme_id()
                //           << ", StartTime=" << start_time_sec
                //           << "s, Duration=" << viseme.duration_sec() << "s" << std::endl;
                webrtc_handler_->SendVisemeData(current_session_id, viseme.viseme_id(), start_time_sec, viseme.duration_sec());
                break;
            }
            case avatar_sync::AvatarSyncStreamRequest::REQUEST_DATA_NOT_SET: {
                std::cerr << "Received request with no data set." << std::endl;
                break;
            }
            default: {
                std::cerr << "Received unknown request data type." << std::endl;
                break;
            }
        }
    }

    std::cout << "Client stream ended for session_id: " << current_session_id << "." << std::endl;
    if (!current_session_id.empty()) {
        // 스트림 종료 시 WebRTC 세션 정리
        // AsyncNotifyWhenDone 콜백에서 처리하거나, 여기서 명시적으로 호출
        // 다만, AsyncNotifyWhenDone이 호출되기 전에 이 부분이 실행될 수 있으므로 중복 호출 방지 필요
        // 여기서는 reader->Read()가 false를 반환했을 때 (정상 종료 또는 에러) 정리
        webrtc_handler_->CleanupSession(current_session_id);
    }
    
    // 클라이언트가 스트리밍을 마치면 빈 응답을 반환합니다.
    return grpc::Status::OK;
}

} // namespace avatar_sync_service