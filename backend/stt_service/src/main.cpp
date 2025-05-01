// src/main.cpp for stt-service

#include <grpcpp/grpcpp.h>
#include "azure_stt_client.h"   // Include Azure STT client
#include "llm_engine_client.h"  // Include LLM engine client
#include "stt_service.h"      // Include STT service implementation
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <memory>  // For std::shared_ptr
#include <stdexcept> // For std::runtime_error

// .env 파일 로딩 함수 (tts_service/src/main.cpp 에서 복사)
void loadDotEnv(const std::string& path = ".env") {
    std::ifstream env(path);
    if (!env.is_open()) {
        // std::cerr << "ℹ️ .env file not found at " << path << ", relying on environment variables." << std::endl;
        return;
    }
    std::string line;
    while (std::getline(env, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);
        // 간단한 trim
        key.erase(0, key.find_first_not_of(" \t\r\n"));
        key.erase(key.find_last_not_of(" \t\r\n") + 1);
        val.erase(0, val.find_first_not_of(" \t\r\n"));
        val.erase(val.find_last_not_of(" \t\r\n") + 1);

        if (getenv(key.c_str()) == nullptr) {
            #ifdef _WIN32
                _putenv_s(key.c_str(), val.c_str());
            #else
                setenv(key.c_str(), val.c_str(), 0); // 0: 기존 환경 변수 덮어쓰지 않음
            #endif
        }
    }
}

int main() {
    loadDotEnv(); // .env 파일 로드 시도

    // --- 환경 변수 로드 ---
    const char* azure_key = std::getenv("AZURE_SPEECH_KEY");
    const char* azure_region = std::getenv("AZURE_SPEECH_REGION");
    const char* llm_addr_env = std::getenv("LLM_ENGINE_ADDRESS"); // LLM 엔진 주소
    const char* server_addr_env = std::getenv("STT_SERVER_ADDRESS"); // STT 서버 리스닝 주소

    // 필수 환경 변수 확인
    if (!azure_key || !azure_region || std::string(azure_key).empty() || std::string(azure_region).empty()) {
        std::cerr << "❌ Missing or empty AZURE_SPEECH_KEY or AZURE_SPEECH_REGION environment variables.\n";
        return 1;
    }
     if (!llm_addr_env || std::string(llm_addr_env).empty()) {
        std::cerr << "❌ Missing or empty LLM_ENGINE_ADDRESS environment variable.\n";
        return 1;
    }

    std::string llm_engine_address = llm_addr_env;
    // STT 서버 주소 (기본값: 0.0.0.0:50056)
    std::string stt_server_address = (server_addr_env && !std::string(server_addr_env).empty())
                                     ? server_addr_env
                                     : "0.0.0.0:50056";

    std::cout << "🔑 Using Azure Speech Region: " << azure_region << std::endl;
    std::cout << "🔗 LLM Engine Address: " << llm_engine_address << std::endl;
    std::cout << "👂 STT Service Listening Address: " << stt_server_address << std::endl;

    try {
        // --- 클라이언트 인스턴스 생성 (shared_ptr 사용) ---
        // Azure STT 클라이언트 생성
        auto azure_client = std::make_shared<stt::AzureSTTClient>(azure_key, azure_region);
        // LLM Engine 클라이언트 생성
        auto llm_client = std::make_shared<stt::LLMEngineClient>(llm_engine_address);

        // --- gRPC 서비스 구현체 생성 (클라이언트 의존성 주입) ---
        stt::STTServiceImpl service(azure_client, llm_client);

        // --- gRPC 서버 설정 및 시작 ---
        grpc::ServerBuilder builder;
        // InsecureCredentials 사용 (필요시 SecureCredentials 설정)
        builder.AddListeningPort(stt_server_address, grpc::InsecureServerCredentials());
        // STT 서비스 등록
        builder.RegisterService(&service);

        // 서버 빌드 및 시작
        std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
        if (server) {
            std::cout << "✅ STT gRPC server listening at " << stt_server_address << std::endl;
            // 서버 종료 시까지 대기
            server->Wait();
        } else {
            std::cerr << "❌ Failed to start STT gRPC server on " << stt_server_address << std::endl;
            return 1;
        }

    } catch (const std::exception& e) {
        std::cerr << "❌ Exception during initialization or runtime: " << e.what() << std::endl;
        return 1;
    } catch (...) {
         std::cerr << "❌ Unknown exception caught." << std::endl;
         return 1;
    }

    return 0;
}