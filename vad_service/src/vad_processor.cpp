#include "vad_processor.h"
#include <iostream>
#include <stdexcept>
#include <algorithm> // for std::copy, std::fill

// 생성자: 모델 로드 및 초기화
VadProcessor::VadProcessor(const std::string& model_path, int sample_rate, int frame_size, float threshold, int min_silence_samples, int min_speech_samples)
    : env_(ORT_LOGGING_LEVEL_WARNING, "vad_processor"),
      sample_rate_(sample_rate),
      frame_size_(frame_size), // 모델에 맞는 값으로 설정!
      threshold_(threshold),
      min_silence_samples_(min_silence_samples),
      min_speech_samples_(min_speech_samples),
      input_buffer_(frame_size) // 모델 입력 크기에 맞게
{
    try {
        initialize_onnx(model_path);
        initialize_states(); // 모델 상태 초기화
    } catch (const Ort::Exception& e) {
        throw std::runtime_error("Failed to initialize ONNX Runtime: " + std::string(e.what()));
    } catch (const std::exception& e) {
         throw std::runtime_error("Failed to initialize VadProcessor: " + std::string(e.what()));
    }
    std::cout << "✅ VadProcessor initialized with model: " << model_path << std::endl;
}

VadProcessor::~VadProcessor() = default;

// ONNX 모델 로드 및 설정 (상세 구현 필요)
void VadProcessor::initialize_onnx(const std::string& model_path) {
     // 세션 옵션 설정 (예: 스레딩, 최적화 레벨)
     session_options_.SetIntraOpNumThreads(1);
     session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

     // 모델 파일로부터 세션 생성
     #ifdef _WIN32
         std::wstring model_path_w(model_path.begin(), model_path.end());
         session_ = std::make_unique<Ort::Session>(env_, model_path_w.c_str(), session_options_);
     #else
         session_ = std::make_unique<Ort::Session>(env_, model_path.c_str(), session_options_);
     #endif

     // --- 모델의 입력/출력 노드 이름 및 형태 가져오기 ---
     // 이 부분은 실제 silero_vad.onnx 모델 구조를 확인하고 설정해야 합니다.
     // 예시:
     input_node_names_.push_back("input"); // 실제 모델의 입력 노드 이름
     input_node_names_.push_back("sr");    // 샘플레이트 입력 (텐서 형태)
     input_node_names_.push_back("h");     // 상태 h
     input_node_names_.push_back("c");     // 상태 c
     output_node_names_.push_back("output"); // 실제 모델의 출력 노드 이름
     output_node_names_.push_back("hn");    // 다음 상태 h
     output_node_names_.push_back("cn");    // 다음 상태 c

     // 입력 차원 설정 (예시: batch=1, length=frame_size, features=1)
     input_node_dims_[0] = 1;
     input_node_dims_[1] = frame_size_;
     // input_node_dims_[2] = 1; // 모델에 따라 다름

     // 상태 벡터 크기 가져오기 (모델 구조 확인 필요)
     // Ort::TypeInfo type_info = session_->GetInputTypeInfo(2); // 'h' 노드 정보
     // auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
     // std::vector<int64_t> h_shape = tensor_info.GetShape();
     // h_state_.resize(tensor_info.GetElementCount()); // 예: [2, 1, 64] -> 128
     // c_state_.resize(tensor_info.GetElementCount());
     h_state_.resize(128); // 모델에 맞게 크기 설정! (예: 2x1x64)
     c_state_.resize(128); // 모델에 맞게 크기 설정! (예: 2x1x64)
     std::cout << "   - Input nodes: " << session_->GetInputCount() << ", Output nodes: " << session_->GetOutputCount() << std::endl;
     std::cout << "   - State vector size: " << h_state_.size() << std::endl;
}

// 모델 상태 변수 초기화
void VadProcessor::initialize_states() {
     std::fill(h_state_.begin(), h_state_.end(), 0.0f);
     std::fill(c_state_.begin(), c_state_.end(), 0.0f);
     reset_state(); // 음성 감지 상태도 초기화
}


// VAD 상태 초기화 (묵음/발화 카운터)
void VadProcessor::reset_state() {
    speech_detected_ = false;
    current_silence_samples_ = 0;
    current_speech_samples_ = 0;
     // 모델의 내부 RNN/LSTM 상태도 초기화해야 할 수 있음 (필요 시 initialize_states 호출)
     // std::fill(h_state_.begin(), h_state_.end(), 0.0f);
     // std::fill(c_state_.begin(), c_state_.end(), 0.0f);
}

// 오디오 프레임 처리 및 VAD 수행
bool VadProcessor::process(const std::vector<float>& audio_frame) {
    if (audio_frame.size() != frame_size_) {
        std::cerr << "⚠️ Warning: Audio frame size mismatch! Expected " << frame_size_ << ", got " << audio_frame.size() << std::endl;
        // 크기가 다르면 처리 불가 또는 패딩/자르기 필요
        return speech_detected_;
    }

    // 입력 버퍼 채우기
    std::copy(audio_frame.begin(), audio_frame.end(), input_buffer_.begin());

    // --- ONNX Runtime 추론 준비 ---
    // 입력 텐서 생성 (모델 구조에 맞게!)
    std::vector<Ort::Value> input_tensors;

    // 1. 오디오 데이터
    Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    input_tensors.push_back(Ort::Value::CreateTensor<float>(memory_info, input_buffer_.data(), input_buffer_.size(), input_node_dims_, 2)); // Assuming shape [1, frame_size]

    // 2. 샘플 레이트 (스칼라 텐서)
    std::vector<int64_t> sr_data = { static_cast<int64_t>(sample_rate_) };
    int64_t sr_shape[] = {1}; // Shape for scalar tensor
    input_tensors.push_back(Ort::Value::CreateTensor<int64_t>(memory_info, sr_data.data(), sr_data.size(), sr_shape, 1));


    // 3. 상태 H
    int64_t h_shape[] = {2, 1, 64}; // 모델 h 상태 shape 예시 [layers, batch, hidden_size]
    input_tensors.push_back(Ort::Value::CreateTensor<float>(memory_info, h_state_.data(), h_state_.size(), h_shape, 3));

    // 4. 상태 C
    int64_t c_shape[] = {2, 1, 64}; // 모델 c 상태 shape 예시
    input_tensors.push_back(Ort::Value::CreateTensor<float>(memory_info, c_state_.data(), c_state_.size(), c_shape, 3));


    // --- ONNX Runtime 추론 실행 ---
    auto output_tensors = session_->Run(Ort::RunOptions{nullptr},
                                       input_node_names_.data(), input_tensors.data(), input_tensors.size(),
                                       output_node_names_.data(), output_node_names_.size());

    // --- 추론 결과 처리 ---
    // 1. 음성 확률 값 가져오기
    float* output_data = output_tensors[0].GetTensorMutableData<float>();
    float speech_prob = output_data[0]; // 출력이 스칼라라고 가정

    // 2. 다음 상태 업데이트 (h_n, c_n)
    float* hn_data = output_tensors[1].GetTensorMutableData<float>();
    float* cn_data = output_tensors[2].GetTensorMutableData<float>();
    std::copy(hn_data, hn_data + h_state_.size(), h_state_.begin());
    std::copy(cn_data, cn_data + c_state_.size(), c_state_.begin());


    // --- VAD 상태 머신 로직 ---
    if (speech_prob >= threshold_) {
        current_speech_samples_ += frame_size_;
        current_silence_samples_ = 0;
        if (current_speech_samples_ >= min_speech_samples_) {
            speech_detected_ = true;
        }
    } else {
        current_silence_samples_ += frame_size_;
        current_speech_samples_ = 0;
        if (current_silence_samples_ >= min_silence_samples_) {
             if (speech_detected_) { // 이전 프레임까지 발화 중이었다면, 이제 발화 종료
                 std::cout << "[VAD] Speech ended." << std::endl;
                 // 필요시 모델 상태 초기화
                 // initialize_states(); // 발화 종료 시 상태 초기화 여부 결정
             }
            speech_detected_ = false;
        }
    }

    return speech_detected_;
}

bool VadProcessor::is_speech() const {
    return speech_detected_;
}