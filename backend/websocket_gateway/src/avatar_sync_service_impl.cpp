// src/avatar_sync_service_impl.cpp
#include "avatar_sync_service_impl.h" // 여기에는 ConsistentWebSocketConnection 정의가 포함되어 있어야 함
// websocket_server.h 를 직접 포함하여 GLOBAL 상수들을 가져오거나,
// avatar_sync_service_impl.h 에서 ConsistentWebSocketConnection 타입을 잘 정의했는지 확인
#include "websocket_server.h" // ConsistentWebSocketConnection 타입과 GLOBAL 상수들을 사용하기 위함
#include "types.h"             // PerSocketData
#include <iostream>
#include "google/protobuf/empty.pb.h"
#include <nlohmann/json.hpp>

AvatarSyncServiceImpl::AvatarSyncServiceImpl(WebSocketFinder finder)
    : find_websocket_by_session_id_(std::move(finder)) {
    std::cout << "AvatarSyncServiceImpl initialized." << std::endl;
}

grpc::Status AvatarSyncServiceImpl::SyncAvatarStream(
    grpc::ServerContext* context,
    grpc::ServerReader<avatar_sync::AvatarSyncStreamRequest>* reader,
    google::protobuf::Empty* /*response*/)
{
    avatar_sync::AvatarSyncStreamRequest request;
    std::string current_session_id;
    // ConsistentWebSocketConnection 타입을 직접 사용합니다.
    ConsistentWebSocketConnection* ws = nullptr; // 타입은 avatar_sync_service_impl.h 에 정의된 것을 사용

    std::cout << "AvatarSyncServiceImpl: incoming stream from " << context->peer() << std::endl;

    while (reader->Read(&request)) {
        if (context->IsCancelled()) {
            return grpc::Status(grpc::StatusCode::CANCELLED, "Client cancelled");
        }

        switch (request.request_data_case()) {
            case avatar_sync::AvatarSyncStreamRequest::kConfig: {
                current_session_id = request.config().session_id();
                ws = find_websocket_by_session_id_(current_session_id);
                if (!ws) {
                    std::cerr << "[" << current_session_id << "] WebSocket not found for AvatarSync\n";
                } else {
                    std::cout << "[" << current_session_id << "] WebSocket found for AvatarSync.\n";
                }
                break;
            }
            case avatar_sync::AvatarSyncStreamRequest::kAudioChunk: {
                if (ws) {
                    auto& audio = request.audio_chunk();
                    // std::cout << "[" << current_session_id << "] Sending audio chunk via WebSocket, size: " << audio.data().size() << std::endl;
                    ws->send(audio.data(), uWS::OpCode::BINARY);
                }
                break;
            }
            case avatar_sync::AvatarSyncStreamRequest::kVisemeData: {
                if (ws) {
                    auto& vis = request.viseme_data();
                    nlohmann::json j = {
                        {"type", "viseme"}, // 프론트엔드에서 구분하기 쉽도록 type 필드 추가 고려
                        {"sessionId", current_session_id},
                        {"visemeId", vis.viseme_id()},
                        {"timestampMs", vis.start_time().seconds() * 1000 + vis.start_time().nanos() / 1000000},
                        {"durationSec", vis.duration_sec()}
                    };
                    auto s = j.dump();
                    // std::cout << "[" << current_session_id << "] Sending viseme data via WebSocket: " << s << std::endl;
                    ws->send(s, uWS::OpCode::TEXT);
                }
                break;
            }
            // REQUEST_DATA_NOT_SET 또는 case avatar_sync::AvatarSyncStreamRequest::REQUEST_DATA_NOT_SET:
            default:
                // std::cerr << "[" << current_session_id << "] Unknown or not set data in AvatarSyncStreamRequest" << std::endl;
                break;
        }
    }

    std::cout << "AvatarSyncServiceImpl: stream closed for session " << current_session_id << std::endl;
    return grpc::Status::OK;
}