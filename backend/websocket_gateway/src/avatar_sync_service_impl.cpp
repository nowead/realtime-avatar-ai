// src/avatar_sync_service_impl.cpp
#include "avatar_sync_service_impl.h" // 해당 클래스의 헤더 파일을 가장 먼저 포함하는 것이 일반적입니다.

#include <iostream>
#include "google/protobuf/empty.pb.h" // google::protobuf::Empty 사용
#include <nlohmann/json.hpp>          // JSON 처리 (필요시)

// websocket_server.h 와 types.h 는 avatar_sync_service_impl.h 를 통해 간접적으로 포함됩니다.
// 만약 명시적으로 필요하다면 여기에 추가할 수 있으나, 헤더 파일 내에서 이미 포함되어야 합니다.
// #include "websocket_server.h"
// #include "types.h" 

namespace websocket_gateway { 

AvatarSyncServiceImpl::AvatarSyncServiceImpl(WebSocketFinder finder)
    : find_websocket_by_session_id_(std::move(finder)) {
    if (!find_websocket_by_session_id_) { // 콜백 유효성 검사
        throw std::runtime_error("WebSocketFinder callback cannot be null in AvatarSyncServiceImpl constructor.");
    }
    std::cout << "AvatarSyncServiceImpl initialized." << std::endl;
}

grpc::Status AvatarSyncServiceImpl::SyncAvatarStream(
    grpc::ServerContext* context,
    grpc::ServerReader<avatar_sync::AvatarSyncStreamRequest>* reader,
    google::protobuf::Empty* /*response*/)
{
    avatar_sync::AvatarSyncStreamRequest request;
    std::string current_frontend_session_id; 
    // ConsistentWebSocketConnection 타입은 헤더에서 using으로 정의됨
    ConsistentWebSocketConnection* ws = nullptr; 

    std::cout << "AvatarSyncServiceImpl: incoming gRPC stream from TTS service (peer: " << context->peer() << ")" << std::endl;

    while (reader->Read(&request)) {
        if (context->IsCancelled()) {
            std::cout << "AvatarSyncService: [" << (current_frontend_session_id.empty() ? "UNKNOWN_SESSION" : current_frontend_session_id) 
                      << "] Client (TTS service) cancelled the gRPC stream." << std::endl;
            return grpc::Status(grpc::StatusCode::CANCELLED, "Client (TTS service) cancelled gRPC stream");
        }

        switch (request.request_data_case()) {
            case avatar_sync::AvatarSyncStreamRequest::kConfig: {
                // proto에서 SyncConfig의 필드명이 frontend_session_id라고 가정
                current_frontend_session_id = request.config().frontend_session_id(); 
                if (current_frontend_session_id.empty()) {
                    std::cerr << "AvatarSyncService: Received SyncConfig with empty frontend_session_id from TTS service." << std::endl;
                    // 이 경우 오류로 처리하고 스트림을 종료할 수 있습니다.
                    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "SyncConfig must contain a valid frontend_session_id.");
                }
                std::cout << "AvatarSyncService: [" << current_frontend_session_id << "] Received SyncConfig. Attempting to find WebSocket connection." << std::endl;
                ws = find_websocket_by_session_id_(current_frontend_session_id); 
                if (!ws) {
                    std::cerr << "AvatarSyncService: [" << current_frontend_session_id << "] ❌ WebSocket connection NOT FOUND for frontend_session_id." << std::endl;
                    // TTS 서비스에게 웹소켓을 찾을 수 없음을 알리고 스트림을 종료하는 것이 좋습니다.
                    // return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "WebSocket session not found for ID: " + current_frontend_session_id);
                } else {
                    std::cout << "AvatarSyncService: [" << current_frontend_session_id << "] ✅ WebSocket connection FOUND." << std::endl;
                }
                break;
            }
            case avatar_sync::AvatarSyncStreamRequest::kAudioChunk: {
                if (ws) { 
                    const auto& audio_bytes_str = request.audio_chunk(); // bytes 필드는 std::string으로 매핑됨
                    // 상세 로깅은 필요시에만 활성화 (성능 영향 가능성)
                    // std::cout << "AvatarSyncService: [" << current_frontend_session_id << "] Received Audio Chunk from TTS. Size: " << audio_bytes_str.size() << ". Sending to WebSocket." << std::endl;
                    ws->send(std::string_view(audio_bytes_str.data(), audio_bytes_str.size()), uWS::OpCode::BINARY);
                } else {
                    std::cerr << "AvatarSyncService: [" << current_frontend_session_id << "] ❌ Received audio chunk, but WebSocket is NULL (either not found or config not received yet)." << std::endl;
                }
                break;
            }
            case avatar_sync::AvatarSyncStreamRequest::kVisemeData: {
                if (ws) { 
                    const auto& vis = request.viseme_data();
                    nlohmann::json j_payload = {
                        {"type", "viseme"}, // 클라이언트 JS에서 이 type으로 메시지 구분
                        {"sessionId", current_frontend_session_id}, 
                        {"visemeId", vis.viseme_id()},
                        {"timestampMs", vis.start_time().seconds() * 1000 + vis.start_time().nanos() / 1000000},
                        {"durationSec", vis.duration_sec()}
                    };
                    std::string json_str_payload = j_payload.dump(); 
                    // 상세 로깅은 필요시에만 활성화
                    // std::cout << "AvatarSyncService: [" << current_frontend_session_id << "] Received Viseme Data from TTS. ID: " << vis.viseme_id() << ". Sending to WebSocket: " << json_str_payload << std::endl;
                    ws->send(json_str_payload, uWS::OpCode::TEXT); 
                } else {
                    std::cerr << "AvatarSyncService: [" << current_frontend_session_id << "] ❌ Received viseme data, but WebSocket is NULL (either not found or config not received yet)." << std::endl;
                }
                break;
            }
            case avatar_sync::AvatarSyncStreamRequest::REQUEST_DATA_NOT_SET:
                // std::cout << "AvatarSyncService: [" << (current_frontend_session_id.empty() ? "UNKNOWN_SESSION" : current_frontend_session_id) 
                //           << "] Received request with data not set from TTS service." << std::endl;
                break;
            default:
                std::cerr << "AvatarSyncService: [" << (current_frontend_session_id.empty() ? "UNKNOWN_SESSION" : current_frontend_session_id) 
                          << "] Received unknown data type in AvatarSyncStreamRequest from TTS service: " << request.request_data_case() << std::endl;
                break;
        }
    }

    std::cout << "AvatarSyncService: [" << (current_frontend_session_id.empty() ? "UNKNOWN_SESSION" : current_frontend_session_id) 
              << "] gRPC stream closed by client (TTS service)." << std::endl;
    return grpc::Status::OK;
}

} // namespace websocket_gateway
