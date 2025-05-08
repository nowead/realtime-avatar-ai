#include <iostream>
#include <string>
#include <memory>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>

#include "avatar_sync_service.h" // 실제 서비스 구현 헤더
#include "webrtc_handler.h"      // WebRTC 핸들러 헤더

void RunServer() {
    std::string server_address("0.0.0.0:50051"); // 모든 인터페이스에서 수신, 포트 50051

    // WebRTCHandler 인스턴스 생성
    auto webrtc_handler = std::make_shared<WebRTCHandler>();

    // AvatarSyncService 구현 인스턴스 생성 및 WebRTCHandler 주입
    avatar_sync_service::AvatarSyncServiceImpl service_impl(webrtc_handler);

    grpc::EnableDefaultHealthCheckService(true);
    grpc::ProtoServerReflectionPlugin reflection_plugin; // 서버 리플렉션 활성화

    grpc::ServerBuilder builder;
    // SSL/TLS 설정이 필요하다면 여기에 추가합니다.
    // 예: builder.SetSslServerCredentials(...)
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials()); // 현재는 Insecure
    builder.RegisterService(&service_impl);

    // 리플렉션 플러그인 등록
    grpc::Service* reflection_service = reflection_plugin.CreateProtoReflectionService();
    builder.RegisterService(reflection_service);


    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    if (!server) {
        std::cerr << "Server failed to start on " << server_address << std::endl;
        return;
    }
    std::cout << "Server listening on " << server_address << std::endl;

    // 서버가 종료될 때까지 대기
    server->Wait();
}

int main(int argc, char** argv) {
    // WebRTC 라이브러리 초기화 (필요한 경우)
    // 예: rtc::LogMessage::LogToDebug(rtc::LS_INFO);
    // 예: webrtc::InitializeSSL(); 또는 유사한 함수 호출

    RunServer();

    // WebRTC 라이브러리 정리 (필요한 경우)
    // 예: webrtc::CleanupSSL();

    return 0;
}