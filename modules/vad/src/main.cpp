#include "vad_processor.h"
#include "websocket_client.h"
#include "audio_input.h" // 가정: 오디오 입력 클래스
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <csignal> // For Ctrl+C handling

std::atomic<bool> exit_flag = false;

void signalHandler(int signum) {
    std::cout << "\nInterrupt signal (" << signum << ") received." << std::endl;
    exit_flag = true;
}

int main() {
    signal(SIGINT, signalHandler); // Ctrl+C 처리

    try {
        // --- 설정 ---
        std::string model_path = "models/silero_vad.onnx";
        std::string ws_uri = "ws://localhost:9002"; // 연결할 서버 주소
        int sample_rate = 16000;
        int frame_ms = 64; // Silero VAD가 기대하는 프레임 길이 (ms) - 모델 확인 필요!
        int frame_size = static_cast<int>(sample_rate * frame_ms / 1000.0); // 샘플 수

        // --- 객체 생성 ---
        VadProcessor vad(model_path, sample_rate, frame_size);
        WebSocketClient ws_client;
        AudioInput audio_input(sample_rate, frame_size); // 오디오 입력 초기화 (구현 필요)

        // --- WebSocket 핸들러 설정 ---
        ws_client.onConnect([]() {
            std::cout << "[Main] WebSocket connected!" << std::endl;
            // 연결 시 필요한 작업 (예: 등록 메시지 전송)
            // ws_client.sendText("{\"type\": \"register\", \"client_type\": \"vad\"}");
        });
        ws_client.onDisconnect([](int code, std::string_view message) {
            std::cerr << "[Main] WebSocket disconnected: " << message << " (code: " << code << ")" << std::endl;
            // 재연결 로직 등
        });
        ws_client.onMessage([](std::string_view message, uWS::OpCode opCode) {
             std::cout << "[Main] Received WS message: " << message << std::endl;
             // 서버로부터 메시지 처리
        });

        // --- WebSocket 연결 시도 ---
        ws_client.connect(ws_uri);

        // --- 오디오 처리 루프 ---
        std::cout << "Starting audio processing loop (Press Ctrl+C to exit)..." << std::endl;
        std::vector<float> audio_buffer; // VAD 처리를 위한 float 버퍼

        while (!exit_flag) {
            // 1. 오디오 청크 가져오기 (16bit PCM)
            std::vector<int16_t> pcm_chunk = audio_input.getChunk(); // 블로킹 또는 논블로킹

            if (!pcm_chunk.empty()) {
                // 2. 오디오 전처리 (int16 -> float, 정규화 등)
                audio_buffer.resize(pcm_chunk.size());
                for (size_t i = 0; i < pcm_chunk.size(); ++i) {
                    audio_buffer[i] = static_cast<float>(pcm_chunk[i]) / 32768.0f; // 예시: 정규화
                }

                // 3. VAD 처리
                bool is_speech = vad.process(audio_buffer);

                // 4. 음성 감지 시 WebSocket으로 전송
                if (is_speech && ws_client.isConnected()) {
                    // 16bit PCM 데이터를 byte 벡터로 변환
                    std::vector<uint8_t> byte_chunk(pcm_chunk.size() * sizeof(int16_t));
                    std::memcpy(byte_chunk.data(), pcm_chunk.data(), byte_chunk.size());
                    ws_client.sendBinary(byte_chunk);
                     // std::cout << "."; // 발화 중 표시
                } else if (!is_speech && vad.is_speech()) { // 이전 프레임은 발화였는데 지금은 아님 -> 발화 종료 직후
                     // 필요 시 발화 종료 메시지 전송
                     // ws_client.sendText("{\"type\": \"speech_end\"}");
                 }

            } else {
                // 오디오 입력이 없거나 대기 중일 때 잠시 대기
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        std::cout << "Exiting loop..." << std::endl;
        audio_input.stop(); // 오디오 입력 중지
        ws_client.disconnect(); // WebSocket 연결 종료

    } catch (const std::exception& e) {
        std::cerr << "❌ An error occurred: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "Program finished gracefully." << std::endl;
    return 0;
}