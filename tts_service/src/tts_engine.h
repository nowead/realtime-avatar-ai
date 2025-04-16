#pragma once

#include <string>
#include <vector>

// 텍스트를 음성으로 변환하여 지정된 경로에 저장
bool run_open_tts_to_file(const std::string& text, const std::string& output_path);

// 텍스트를 음성으로 변환하여 메모리(std::vector<uint8_t>)로 반환
std::vector<uint8_t> run_open_tts_to_memory(const std::string& text, const std::string& voice = "default");
