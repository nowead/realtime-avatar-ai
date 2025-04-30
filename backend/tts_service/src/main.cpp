// src/main.cpp

#include <grpcpp/grpcpp.h>
#include "azure_tts_client.h"
#include "tts_service.h"
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <cerrno>

// .env 파일을 읽어 환경변수로 설정
void loadDotEnv(const std::string& path = ".env") {
    std::ifstream env(path);
    if (!env.is_open()) {
        std::cerr << "❌ .env open error: "
                  << strerror(errno) << std::endl;
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
        if (std::getenv(key.c_str()) == nullptr)
            setenv(key.c_str(), val.c_str(), 0);
    }
}

int main() {
    // loadDotEnv();

    const char* key    = std::getenv("AZURE_SPEECH_KEY");
    const char* region = std::getenv("AZURE_SPEECH_REGION");
    if (!key || !region) {
        std::cerr << "❌ Missing AZURE_SPEECH_KEY or AZURE_SPEECH_REGION\n";
        return 1;
    }

    std::string addr = "0.0.0.0:50055";
    tts::AzureTTSClient client(key, region);
    tts::TTSServiceImpl service(&client);

    grpc::ServerBuilder builder;
    builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    auto server = builder.BuildAndStart();
    std::cout << "✅ TTS gRPC server listening at " << addr << std::endl;
    server->Wait();
    return 0;
}
