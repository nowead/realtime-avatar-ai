#pragma once

#include <vector>
#include <string>
#include <memory>
#include <onnxruntime_cxx_api.h> // ONNX Runtime 헤더

class VadProcessor {
public:
    VadProcessor(const std::string& model_path, int sample_rate = 16000, int frame_size = 512, float threshold = 0.5f, int min_silence_samples = 8000, int min_speech_samples = 256);
    ~VadProcessor();

    // 오디오 청크를 처리하고 음성 감지 여부 반환 (상태 유지)
    bool process(const std::vector<float>& audio_frame);

    // VAD 상태 초기화
    void reset_state();

    bool is_speech() const;

private:
    // ONNX Runtime 관련 멤버
    Ort::Env env_;
    Ort::SessionOptions session_options_;
    std::unique_ptr<Ort::Session> session_;
    Ort::AllocatorWithDefaultOptions allocator_;
    std::vector<const char*> input_node_names_;
    std::vector<const char*> output_node_names_;

    // VAD 모델 관련 파라미터 및 상태 변수
    int sample_rate_;
    int frame_size_; // Silero VAD가 기대하는 프레임 크기 (모델마다 다름)
    float threshold_;
    int min_silence_samples_; // 묵음 지속 시간 (샘플 수 기준)
    int min_speech_samples_; // 최소 음성 길이 (샘플 수 기준)

    std::vector<float> input_buffer_; // 모델 입력을 위한 버퍼
    std::vector<float> h_state_, c_state_; // 모델의 내부 상태 (RNN/LSTM 등)
    int64_t input_node_dims_[3]; // 예: {1, frame_size, 1} 모델에 따라 조정

    // 음성 감지 상태
    bool speech_detected_ = false;
    int current_silence_samples_ = 0;
    int current_speech_samples_ = 0;

    void initialize_onnx(const std::string& model_path);
    void initialize_states();
};