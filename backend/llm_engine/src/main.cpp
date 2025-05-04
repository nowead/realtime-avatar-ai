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
#include "llm_service.h"
#include "tts_client.h"
#include "openai_client.h"

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
        tts_client = std::make_shared<llm_engine::TTSClient>(tts_service_address);
        std::cout << "✅ TTS client initialized." << std::endl;

        std::cout << "⏳ Initializing OpenAI client..." << std::endl;
        openai_client = std::make_shared<llm_engine::OpenAIClient>(openai_api_key, openai_model);
        std::cout << "✅ OpenAI client initialized." << std::endl;

        std::cout << "⏳ Creating LLM service implementation..." << std::endl;
        service_impl = std::make_unique<llm_engine::LLMServiceImpl>(tts_client, openai_client);
        std::cout << "✅ LLM service implementation created." << std::endl;

        // --- Setup and Start gRPC Server ---
        grpc::ServerBuilder builder;
        builder.AddListeningPort(llm_server_address, grpc::InsecureServerCredentials());
        builder.RegisterService(service_impl.get());

        std::cout << "⏳ Building and starting gRPC server..." << std::endl;
        server_ptr = builder.BuildAndStart(); // Assign to global pointer

        if (server_ptr) {
            std::cout << "✅ LLM gRPC server listening at " << llm_server_address << std::endl;
            // Wait for server shutdown signal
            server_ptr->Wait();
        } else {
            std::cerr << "❌ FATAL: Failed to start LLM gRPC server on " << llm_server_address << ". Exiting." << std::endl;
            return 1;
        }

    } catch (const std::exception& e) {
        std::cerr << "❌ FATAL Exception during initialization: " << e.what() << ". Exiting." << std::endl;
        // Clean up order matters less on fatal exception, but good practice
        server_ptr.reset();
        service_impl.reset();
        openai_client.reset();
        tts_client.reset();
        return 1;
    } catch (...) {
        std::cerr << "❌ FATAL Unknown exception caught during initialization. Exiting." << std::endl;
        server_ptr.reset();
        service_impl.reset();
        openai_client.reset();
        tts_client.reset();
        return 1;
    }

    // --- Graceful Shutdown ---
    std::cout << "ℹ️ Server shutdown sequence initiated." << std::endl;

    // Explicitly release resources (smart pointers handle this, but order can matter)
    service_impl.reset(); // Service goes first, allows ongoing calls to finish within deadline
    std::cout << "  LLM service implementation released." << std::endl;
    openai_client.reset(); // Release clients
    std::cout << "  OpenAI client released." << std::endl;
    tts_client.reset();
    std::cout << "  TTS client released." << std::endl;

    std::cout << "✅ LLM Engine Service shut down gracefully." << std::endl;
    return 0;
}