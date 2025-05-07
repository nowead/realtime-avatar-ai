#include <grpcpp/grpcpp.h>
#include "avatar_sync_client.h" // AvatarSyncClient
#include "azure_tts_engine.h"     // AzureTTSEngine 사용
#include "tts_service.h"        // TTSServiceImpl
#include <cstdlib>              // For std::getenv
#include <iostream>
#include <string>
#include <memory>
#include <stdexcept>
#include <csignal>              // For signal handling
#include <atomic>               // For shutdown flag
#include <thread>               // For shutdown delay

// 전역 종료 플래그 및 서버 포인터 (Graceful Shutdown 용)
std::atomic<bool> shutdown_requested(false);
std::unique_ptr<grpc::Server> server_ptr = nullptr;

// 시그널 핸들러 함수
void signalHandler(int signum) {
    std::cout << "\nℹ️ Interrupt signal (" << signum << ") received. Shutting down TTS Service..." << std::endl;
    shutdown_requested.store(true);
    if (server_ptr) {
        // 즉시 종료 또는 마감 시간 설정 가능
        server_ptr->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(5)); // 5초 유예
    }
}

int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    std::cout << "🚀 Starting TTS Service..." << std::endl;

    // --- 환경 변수 로드 ---
    const char* azure_key_env = std::getenv("AZURE_SPEECH_KEY");
    const char* azure_region_env = std::getenv("AZURE_SPEECH_REGION");
    const char* avatar_sync_addr_env = std::getenv("AVATAR_SYNC_SERVICE_ADDRESS");
    const char* server_addr_env = std::getenv("TTS_SERVER_ADDRESS");

    // 필수 환경 변수 확인
    if (!azure_key_env || std::string(azure_key_env).empty() ||
        !azure_region_env || std::string(azure_region_env).empty()) {
        std::cerr << "❌ FATAL: Missing or empty AZURE_SPEECH_KEY or AZURE_SPEECH_REGION. Exiting." << std::endl;
        return 1;
    }
    if (!avatar_sync_addr_env || std::string(avatar_sync_addr_env).empty()) {
        std::cerr << "❌ FATAL: Missing or empty AVATAR_SYNC_SERVICE_ADDRESS. Exiting." << std::endl;
        return 1;
    }

    std::string azure_speech_key = azure_key_env;
    std::string azure_speech_region = azure_region_env;
    std::string avatar_sync_service_address = avatar_sync_addr_env;
    std::string tts_server_address = (server_addr_env && !std::string(server_addr_env).empty())
                                     ? server_addr_env
                                     : "0.0.0.0:50052"; // 기본 TTS 서비스 포트

    std::cout << "🔧 Configuration:" << std::endl;
    std::cout << "  Azure Speech Region: " << azure_speech_region << std::endl;
    // Azure Key는 로그에 남기지 않는 것이 좋습니다.
    // std::cout << "  Azure Speech Key: [REDACTED]" << std::endl;
    std::cout << "  AvatarSync Service Address: " << avatar_sync_service_address << std::endl;
    std::cout << "  TTS Service Listening Address: " << tts_server_address << std::endl;

    std::shared_ptr<tts::AvatarSyncClient> avatar_s_client = nullptr;
    std::unique_ptr<tts::TTSServiceImpl> service_impl = nullptr;
    // AzureTTSEngine 팩토리에 사용될 키와 지역 정보
    // (팩토리 람다 캡처를 위해 main 스코프에 유지)
    const std::string key_for_factory = azure_speech_key;
    const std::string region_for_factory = azure_speech_region;


    try {
        std::cout << "⏳ Initializing AvatarSync client..." << std::endl;
        avatar_s_client = std::make_shared<tts::AvatarSyncClient>(avatar_sync_service_address);
        std::cout << "✅ AvatarSync client initialized." << std::endl;

        // TTS 엔진 팩토리 (AzureTTSEngine 사용)
        auto tts_engine_factory = [&key_for_factory, &region_for_factory]() -> std::unique_ptr<tts::AzureTTSEngine> {
            // 여기서 AzureTTSEngine 인스턴스를 생성하여 반환
            // 생성자에서 Azure Speech Key와 Region 필요
            return std::make_unique<tts::AzureTTSEngine>(key_for_factory, region_for_factory);
        };
        std::cout << "✅ TTS Engine factory (AzureTTSEngine) configured." << std::endl;

        service_impl = std::make_unique<tts::TTSServiceImpl>(avatar_s_client, tts_engine_factory);
        std::cout << "✅ TTS service implementation created." << std::endl;

        grpc::ServerBuilder builder;
        builder.AddListeningPort(tts_server_address, grpc::InsecureServerCredentials());
        builder.RegisterService(service_impl.get());

        std::cout << "⏳ Building and starting gRPC server for TTS service..." << std::endl;
        server_ptr = builder.BuildAndStart(); // 전역 포인터에 할당

        if (server_ptr) {
            std::cout << "✅ TTS gRPC server listening at " << tts_server_address << std::endl;
            server_ptr->Wait(); // 서버 종료 시까지 대기
        } else {
            std::cerr << "❌ FATAL: Failed to start TTS gRPC server on " << tts_server_address << ". Exiting." << std::endl;
            return 1;
        }

    } catch (const std::exception& e) {
        std::cerr << "❌ FATAL Exception during TTS service initialization: " << e.what() << ". Exiting." << std::endl;
        if(server_ptr) server_ptr->Shutdown();
        server_ptr.reset();
        service_impl.reset();
        avatar_s_client.reset();
        return 1;
    } catch (...) {
         std::cerr << "❌ FATAL Unknown exception caught during TTS service initialization. Exiting." << std::endl;
         if(server_ptr) server_ptr->Shutdown();
         server_ptr.reset();
         service_impl.reset();
         avatar_s_client.reset();
         return 1;
    }

    std::cout << "ℹ️ TTS Server shutdown sequence initiated (Wait() returned)." << std::endl;

    // 명시적 리소스 정리
    service_impl.reset();
    std::cout << "  TTS service implementation released." << std::endl;
    avatar_s_client.reset();
    std::cout << "  AvatarSync client released." << std::endl;
    // TTS 엔진 인스턴스는 service_impl 내부 또는 SynthesizeStream 스코프에서 관리되므로 여기서 직접 해제 불필요

    std::cout << "✅ TTS Service shut down gracefully." << std::endl;
    return 0;
}