// src/main.cpp for stt-service

#include <grpcpp/grpcpp.h>
#include "azure_stt_client.h"
#include "llm_engine_client.h"
#include "stt_service.h"
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <memory>
#include <stdexcept>
#include <csignal> // For signal handling
#include <atomic> // For shutdown flag
#include <thread> // For shutdown delay

// 전역 종료 플래그 및 서버 포인터 (Graceful Shutdown 용)
std::atomic<bool> shutdown_requested(false);
std::unique_ptr<grpc::Server> server_ptr = nullptr;

// 시그널 핸들러 함수
void signalHandler(int signum) {
    std::cout << "\nℹ️ Interrupt signal (" << signum << ") received. Shutting down..." << std::endl;
    shutdown_requested.store(true);
    // 서버 객체가 생성된 후라면 Shutdown 요청
    if (server_ptr) {
        // 즉시 종료 (마감 시간 0) 또는 유예 시간 설정 가능
        // server_ptr->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(5));
        server_ptr->Shutdown(); // 즉시 Shutdown 시작
    }
}

int main() {
    // 시그널 핸들러 등록 (SIGINT, SIGTERM)
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    std::cout << "🚀 Starting STT Service..." << std::endl;

    // --- 환경 변수 로드 ---
    const char* azure_key_env = std::getenv("AZURE_SPEECH_KEY");
    const char* azure_region_env = std::getenv("AZURE_SPEECH_REGION");
    const char* llm_addr_env = std::getenv("LLM_ENGINE_ADDRESS");
    const char* server_addr_env = std::getenv("STT_SERVER_ADDRESS");

    // 필수 환경 변수 확인
    if (!azure_key_env || !azure_region_env || std::string(azure_key_env).empty() || std::string(azure_region_env).empty()) {
        std::cerr << "❌ FATAL: Missing or empty AZURE_SPEECH_KEY or AZURE_SPEECH_REGION environment variables. Exiting." << std::endl;
        return 1;
    }
     if (!llm_addr_env || std::string(llm_addr_env).empty()) {
        std::cerr << "❌ FATAL: Missing or empty LLM_ENGINE_ADDRESS environment variable. Exiting." << std::endl;
        return 1;
    }

    std::string azure_key = azure_key_env;
    std::string azure_region = azure_region_env;
    std::string llm_engine_address = llm_addr_env;
    std::string stt_server_address = (server_addr_env && !std::string(server_addr_env).empty())
                                     ? server_addr_env
                                     : "0.0.0.0:50056"; // 기본값 설정

    std::cout << "🔧 Configuration:" << std::endl;
    std::cout << "  Azure Region: " << azure_region << std::endl;
    // Azure Key는 로그에 남기지 않는 것이 좋습니다.
    // std::cout << "  Azure Key: [REDACTED]" << std::endl;
    std::cout << "  LLM Engine Address: " << llm_engine_address << std::endl;
    std::cout << "  STT Service Listening Address: " << stt_server_address << std::endl;

    // 클라이언트 및 서비스 포인터 (Graceful Shutdown 위해 main 스코프에 선언)
    std::shared_ptr<stt::AzureSTTClient> azure_client = nullptr;
    std::shared_ptr<stt::LLMEngineClient> llm_client = nullptr;
    std::unique_ptr<stt::STTServiceImpl> service_impl = nullptr;


    try {
        // --- 클라이언트 인스턴스 생성 ---
        std::cout << "⏳ Initializing Azure STT client..." << std::endl;
        azure_client = std::make_shared<stt::AzureSTTClient>(azure_key, azure_region);
        std::cout << "✅ Azure STT client initialized." << std::endl;

        std::cout << "⏳ Initializing LLM Engine client..." << std::endl;
        llm_client = std::make_shared<stt::LLMEngineClient>(llm_engine_address);
        std::cout << "✅ LLM Engine client initialized." << std::endl;

        // --- gRPC 서비스 구현체 생성 ---
        service_impl = std::make_unique<stt::STTServiceImpl>(azure_client, llm_client);
        std::cout << "✅ STT service implementation created." << std::endl;

        // --- gRPC 서버 설정 및 시작 ---
        grpc::EnableDefaultHealthCheckService(true); // Health Check 서비스 활성화
    
        grpc::ServerBuilder builder;
        builder.AddListeningPort(stt_server_address, grpc::InsecureServerCredentials());
        builder.RegisterService(service_impl.get()); // unique_ptr의 get() 사용

        std::cout << "⏳ Building and starting gRPC server..." << std::endl;
        server_ptr = builder.BuildAndStart(); // 전역 포인터에 할당

        if (server_ptr) {
            std::cout << "✅ STT gRPC server listening at " << stt_server_address << std::endl;
            // 서버 종료 시까지 대기 (Wait()는 blocking call)
            // 시그널 핸들러가 Shutdown()을 호출하면 Wait()는 리턴함
            server_ptr->Wait();
        } else {
            std::cerr << "❌ FATAL: Failed to start STT gRPC server on " << stt_server_address << ". Exiting." << std::endl;
            return 1;
        }

    } catch (const std::exception& e) {
        std::cerr << "❌ FATAL Exception during initialization: " << e.what() << ". Exiting." << std::endl;
        // 예외 발생 시에도 생성된 객체가 있다면 소멸자 호출 시도
        server_ptr.reset(); // 서버 종료 (필요 시)
        service_impl.reset();
        llm_client.reset();
        azure_client.reset();
        return 1;
    } catch (...) {
         std::cerr << "❌ FATAL Unknown exception caught during initialization. Exiting." << std::endl;
         server_ptr.reset();
         service_impl.reset();
         llm_client.reset();
         azure_client.reset();
         return 1;
    }

    // --- Graceful Shutdown 처리 ---
    std::cout << "ℹ️ Server shutdown sequence initiated." << std::endl;

    // 명시적 리소스 정리 (shared_ptr/unique_ptr가 자동으로 처리하지만, 순서 보장 위해 명시 가능)
    // 서비스 구현 객체 먼저 소멸 (진행 중인 요청 마무리 시도 후)
    service_impl.reset();
    std::cout << "  STT service implementation released." << std::endl;
    // 클라이언트 객체 소멸 (소멸자에서 연결 정리 등 수행)
    llm_client.reset();
    std::cout << "  LLM Engine client released." << std::endl;
    azure_client.reset(); // Azure 클라이언트 소멸자에서 Stop 시도
    std::cout << "  Azure STT client released." << std::endl;

    std::cout << "✅ STT Service shut down gracefully." << std::endl;
    return 0;
}