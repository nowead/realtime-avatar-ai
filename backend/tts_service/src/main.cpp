// src/main.cpp (변경 없음)

#include <grpcpp/grpcpp.h>
#include "azure_tts_client.h"
#include "tts_service.h"
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <cerrno> // for strerror

// .env 파일 로딩 함수 (기존과 동일)
void loadDotEnv(const std::string& path = ".env") {
    // ... (기존 코드와 동일) ...
     std::ifstream env(path);
    if (!env.is_open()) {
        // 파일 없음을 오류로 간주하지 않음 (환경변수 직접 설정 가능)
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
        // trim
        auto trim = [](std::string& s) {
            const char* ws = " \t\r\n";
            s.erase(0, s.find_first_not_of(ws));
            s.erase(s.find_last_not_of(ws) + 1);
        };
        trim(key); trim(val);
        // 환경 변수가 이미 설정되어 있지 않은 경우에만 .env 값 사용
        if (getenv(key.c_str()) == nullptr) {
            #ifdef _WIN32
                _putenv_s(key.c_str(), val.c_str());
            #else
                setenv(key.c_str(), val.c_str(), 0); // 0: do not overwrite existing env var
            #endif
        }
    }
}


int main() {
    loadDotEnv(); // .env 파일 로드 시도

    const char* key    = std::getenv("AZURE_SPEECH_KEY");
    const char* region = std::getenv("AZURE_SPEECH_REGION");
    if (!key || !region || std::string(key).empty() || std::string(region).empty()) {
        std::cerr << "❌ Missing or empty AZURE_SPEECH_KEY or AZURE_SPEECH_REGION environment variables.\n";
        return 1;
    }
     std::cout << "🔑 Using Azure Speech Region: " << region << std::endl;


    std::string addr = "0.0.0.0:50055";
    tts::AzureTTSClient client(key, region); // Azure 클라이언트 생성
    tts::TTSServiceImpl service(&client);   // 서비스 구현체 생성 (수정된 버전)

    grpc::ServerBuilder builder;
    builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&service); // 서비스 등록

    auto server = builder.BuildAndStart();
    if (server) {
        std::cout << "✅ TTS gRPC streaming server listening at " << addr << std::endl;
        server->Wait(); // 서버 종료 대기
    } else {
        std::cerr << "❌ Failed to start gRPC server." << std::endl;
        return 1;
    }

    return 0;
}