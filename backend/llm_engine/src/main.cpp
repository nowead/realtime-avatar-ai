#include <grpcpp/grpcpp.h>
#include <cstdlib>
#include <iostream>
#include <string>
#include <memory>
#include <stdexcept>
#include <csignal>
#include <atomic>
#include <thread>

#include "llm_service.h"

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

    const char* openai_key_env = std::getenv("OPENAI_API_KEY");
    const char* tts_addr_env = std::getenv("TTS_SERVICE_ADDRESS");
    const char* server_addr_env = std::getenv("LLM_SERVER_ADDRESS");
    const char* openai_model_env = std::getenv("OPENAI_MODEL");

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
                                     ? server_addr_env : "0.0.0.0:50053";
    std::string openai_model = (openai_model_env && !std::string(openai_model_env).empty())
                                ? openai_model_env : "gpt-4o";


    std::cout << "🔧 Configuration:" << std::endl;
    std::cout << "  OpenAI Model: " << openai_model << std::endl;
    std::cout << "  TTS Service Address: " << tts_service_address << std::endl;
    std::cout << "  LLM Service Listening Address: " << llm_server_address << std::endl;

    std::shared_ptr<llm_engine::TTSClient> tts_client = nullptr;
    std::shared_ptr<llm_engine::OpenAIClient> openai_client = nullptr;
    std::unique_ptr<llm_engine::LLMServiceImpl> service_impl = nullptr;

    try {
        std::cout << "⏳ Initializing TTS client..." << std::endl;
        std::cout << "   Creating gRPC channel for TTS server at: " << tts_service_address << std::endl;
 
        std::shared_ptr<grpc::Channel> tts_channel = grpc::CreateChannel(
            tts_service_address, grpc::InsecureChannelCredentials()
        );

        if (!tts_channel) {
            // 채널 생성 실패 시 오류 처리
            throw std::runtime_error("Failed to create gRPC channel for TTS server at " + tts_service_address);
        }
        std::cout << "   gRPC channel for TTS created." << std::endl;

        tts_client = std::make_shared<llm_engine::TTSClient>(tts_channel);
        std::cout << "✅ TTS client initialized." << std::endl;
        std::cout << "⏳ Initializing OpenAI client..." << std::endl;
        openai_client = std::make_shared<llm_engine::OpenAIClient>(openai_api_key, openai_model);
        std::cout << "✅ OpenAI client initialized." << std::endl;
        std::cout << "⏳ Creating LLM service implementation..." << std::endl;
        service_impl = std::make_unique<llm_engine::LLMServiceImpl>(tts_client, openai_client);
        std::cout << "✅ LLM service implementation created." << std::endl;

        grpc::EnableDefaultHealthCheckService(true);
        grpc::ServerBuilder builder;

        builder.AddListeningPort(llm_server_address, grpc::InsecureServerCredentials());
        builder.RegisterService(service_impl.get());

        std::cout << "⏳ Building and starting gRPC server..." << std::endl;
        server_ptr = builder.BuildAndStart();

        if (server_ptr) {
            std::cout << "✅ LLM gRPC server listening at " << llm_server_address << std::endl;
            server_ptr->Wait();
            std::cout << "ℹ️ Server Wait() returned. Proceeding with shutdown..." << std::endl;
        } else {
            throw std::runtime_error("Failed to start LLM gRPC server on " + llm_server_address);
        }

    } catch (const std::exception& e) {
        std::cerr << "❌ FATAL Exception during initialization or runtime: " << e.what() << ". Exiting." << std::endl;
        server_ptr.reset();
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

    std::cout << "ℹ️ Server shutdown sequence initiated." << std::endl;

    service_impl.reset();
    std::cout << "  LLM service implementation released." << std::endl;
    openai_client.reset();
    std::cout << "  OpenAI client released." << std::endl;
    tts_client.reset();
    std::cout << "  TTS client released." << std::endl;
    server_ptr.reset();
    std::cout << "  gRPC server pointer released." << std::endl;


    std::cout << "✅ LLM Engine Service shut down gracefully." << std::endl;
    return 0;
}