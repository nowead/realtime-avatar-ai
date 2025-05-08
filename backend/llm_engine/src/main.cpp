#include <grpcpp/grpcpp.h>
#include <cstdlib> // For getenv
#include <iostream>
#include <string>
#include <memory>
#include <stdexcept>
#include <csignal> // For signal handling
#include <atomic>
#include <thread> // For sleep_for

// Include our service and client headers
// llm_service.h 가 다른 헤더들을 포함하므로, 아래 두 개는 필요 없을 수 있음
#include "llm_service.h"
// #include "tts_client.h" // llm_service.h 에서 포함됨 (아마도)
// #include "openai_client.h" // llm_service.h 에서 포함됨 (아마도)

// Global shutdown flag and server pointer
std::atomic<bool> shutdown_requested(false);
std::unique_ptr<grpc::Server> server_ptr = nullptr;

// Signal handler
void signalHandler(int signum) {
    std::cout << "\nℹ️ Interrupt signal (" << signum << ") received. Shutting down LLM engine..." << std::endl;
    shutdown_requested.store(true);
    if (server_ptr) {
        // Use a deadline for graceful shutdown (e.g., 5 seconds)
        server_ptr->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(5));
        // Alternatively, immediate shutdown: server_ptr->Shutdown();
    }
}

int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    std::cout << "🚀 Starting LLM Engine Service..." << std::endl;

    // --- Load Environment Variables ---
    const char* openai_key_env = std::getenv("OPENAI_API_KEY");
    const char* tts_addr_env = std::getenv("TTS_SERVICE_ADDRESS");
    const char* server_addr_env = std::getenv("LLM_SERVER_ADDRESS");
    const char* openai_model_env = std::getenv("OPENAI_MODEL"); // Optional

    // Mandatory variables
    if (!openai_key_env || std::string(openai_key_env).empty()) {
        std::cerr << "❌ FATAL: Missing or empty OPENAI_API_KEY environment variable. Exiting." << std::endl;
        return 1;
    }
    if (!tts_addr_env || std::string(tts_addr_env).empty()) {
        std::cerr << "❌ FATAL: Missing or empty TTS_SERVICE_ADDRESS environment variable. Exiting." << std::endl;
        return 1;
    }

    std::string openai_api_key = openai_key_env;
    std::string tts_service_address = tts_addr_env;
    std::string llm_server_address = (server_addr_env && !std::string(server_addr_env).empty())
                                     ? server_addr_env : "0.0.0.0:50052"; // Default port for LLM
    std::string openai_model = (openai_model_env && !std::string(openai_model_env).empty())
                                ? openai_model_env : "gpt-4o"; // Default model


    std::cout << "🔧 Configuration:" << std::endl;
    // std::cout << "  OpenAI API Key: [REDACTED]" << std::endl; // Don't log the key
    std::cout << "  OpenAI Model: " << openai_model << std::endl;
    std::cout << "  TTS Service Address: " << tts_service_address << std::endl;
    std::cout << "  LLM Service Listening Address: " << llm_server_address << std::endl;

    // Pointers for graceful shutdown
    std::shared_ptr<llm_engine::TTSClient> tts_client = nullptr;
    std::shared_ptr<llm_engine::OpenAIClient> openai_client = nullptr;
    std::unique_ptr<llm_engine::LLMServiceImpl> service_impl = nullptr;

    try {
        // --- Instantiate Clients and Service ---
        std::cout << "⏳ Initializing TTS client..." << std::endl;

        // ===== TTSClient 생성 방식 수정 =====
        // 1. TTS 서버 주소를 사용하여 gRPC 채널 생성
        std::cout << "   Creating gRPC channel for TTS server at: " << tts_service_address << std::endl;
        //    InsecureChannelCredentials 사용 (필요시 SecureCredentials 로 변경)
        std::shared_ptr<grpc::Channel> tts_channel = grpc::CreateChannel(
            tts_service_address, grpc::InsecureChannelCredentials()
        );

        if (!tts_channel) {
            // 채널 생성 실패 시 오류 처리
            throw std::runtime_error("Failed to create gRPC channel for TTS server at " + tts_service_address);
        }
        std::cout << "   gRPC channel for TTS created." << std::endl;

        // 2. 생성된 채널을 사용하여 TTSClient 객체 생성 (std::make_shared 사용)
        //    이제 std::shared_ptr<grpc::Channel> 을 받는 생성자가 호출됨
        tts_client = std::make_shared<llm_engine::TTSClient>(tts_channel);
        std::cout << "✅ TTS client initialized." << std::endl;
        // ===== End TTSClient 생성 방식 수정 =====


        std::cout << "⏳ Initializing OpenAI client..." << std::endl;
        openai_client = std::make_shared<llm_engine::OpenAIClient>(openai_api_key, openai_model);
        std::cout << "✅ OpenAI client initialized." << std::endl;

        std::cout << "⏳ Creating LLM service implementation..." << std::endl;
        // LLMServiceImpl 생성자에 수정된 tts_client 전달
        service_impl = std::make_unique<llm_engine::LLMServiceImpl>(tts_client, openai_client);
        std::cout << "✅ LLM service implementation created." << std::endl;

        // --- Setup and Start gRPC Server ---
        grpc::ServerBuilder builder;
        grpc::EnableDefaultHealthCheckService(true);

        builder.AddListeningPort(llm_server_address, grpc::InsecureServerCredentials());
        builder.RegisterService(service_impl.get());

        std::cout << "⏳ Building and starting gRPC server..." << std::endl;
        server_ptr = builder.BuildAndStart(); // Assign to global pointer

        if (server_ptr) {
            std::cout << "✅ LLM gRPC server listening at " << llm_server_address << std::endl;
            // 서버 종료 시그널 대기 (Wait는 블로킹 함수)
            server_ptr->Wait();
            // Wait() 함수가 반환되면 종료 시퀀스 시작
            std::cout << "ℹ️ Server Wait() returned. Proceeding with shutdown..." << std::endl;
        } else {
            // 서버 시작 실패
            throw std::runtime_error("Failed to start LLM gRPC server on " + llm_server_address);
        }

    } catch (const std::exception& e) {
        std::cerr << "❌ FATAL Exception during initialization or runtime: " << e.what() << ". Exiting." << std::endl;
        // Clean up order matters less on fatal exception
        server_ptr.reset(); // Ensure server is reset if already created
        service_impl.reset();
        openai_client.reset();
        tts_client.reset();
        return 1;
    } catch (...) {
        std::cerr << "❌ FATAL Unknown exception caught during initialization or runtime. Exiting." << std::endl;
        server_ptr.reset();
        service_impl.reset();
        openai_client.reset();
        tts_client.reset();
        return 1;
    }

    // --- Graceful Shutdown (Wait() 반환 후 또는 시그널 핸들러에서 Shutdown() 호출 후 도달) ---
    std::cout << "ℹ️ Server shutdown sequence initiated." << std::endl;

    // 명시적으로 리소스 해제 (스마트 포인터가 처리하지만, 순서가 중요할 수 있음)
    // server_ptr->Shutdown() 은 이미 signalHandler 또는 Wait() 반환 전에 호출되었어야 함
    service_impl.reset(); // 서비스 종료 -> 진행 중인 호출 완료 대기 (Shutdown deadline 내에서)
    std::cout << "  LLM service implementation released." << std::endl;
    openai_client.reset(); // 클라이언트 종료
    std::cout << "  OpenAI client released." << std::endl;
    tts_client.reset();
    std::cout << "  TTS client released." << std::endl;
    server_ptr.reset(); // 서버 포인터 최종 해제
    std::cout << "  gRPC server pointer released." << std::endl;


    std::cout << "✅ LLM Engine Service shut down gracefully." << std::endl;
    return 0;
}