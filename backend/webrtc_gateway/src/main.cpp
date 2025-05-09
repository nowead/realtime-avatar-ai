// src/main.cpp
#include <iostream>
#include <fstream>

#include <boost/asio.hpp>
#include <yaml-cpp/yaml.h>

#include "grpc_clients/SttClient.h"
#include "grpc_clients/TtsClient.h"
#include "rtc/PeerConnectionManager.h"
#include "signaling/WebSocketServer.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: webrtc_gateway <config.yaml>\n";
        return 1;
    }

    // 1) 설정 파일 로딩
    std::string config_path = argv[1];
    YAML::Node config;
    try {
        config = YAML::LoadFile(config_path);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load config: " << e.what() << "\n";
        return 1;
    }

    // 2) STT/TTS 서비스 주소 읽기
    std::string stt_addr = config["stt_service"]["address"].as<std::string>();
    std::string tts_addr = config["tts_service"]["address"].as<std::string>();

    // 3) gRPC 채널 및 클라이언트 생성
    auto stt_channel = grpc::CreateChannel(stt_addr, grpc::InsecureChannelCredentials());
    auto tts_channel = grpc::CreateChannel(tts_addr, grpc::InsecureChannelCredentials());
    auto stt_client  = std::make_shared<SttClient>(stt_channel);
    auto tts_client  = std::make_shared<TtsClient>(tts_channel);

    // 4) PeerConnectionManager 초기화
    auto pcmgr = std::make_shared<rtc::PeerConnectionManager>(stt_client, tts_client);

    // 5) Boost.Asio I/O 컨텍스트 및 WebSocketServer 실행
    boost::asio::io_context ioc;
    int port = config["server"]["websocket_port"].as<int>(8443);
    boost::asio::ip::tcp::endpoint endpoint{boost::asio::ip::tcp::v4(), static_cast<unsigned short>(port)};
    signaling::WebSocketServer wss(ioc, endpoint, pcmgr);
    wss.run();

    std::cout << "webrtc_gateway listening on port " << port << "\n";
    ioc.run();

    return 0;
}
