#include "azure_tts_engine.h"
#include <iostream>
#include <stdexcept> // For std::runtime_error
#include <chrono>    // For time conversions

// Azure Speech SDK 네임스페이스
using namespace Microsoft::CognitiveServices::Speech;

namespace tts {

AzureTTSEngine::AzureTTSEngine(const std::string& key, const std::string& region)
  : subscription_key_(key), region_(region) {
    try {
        speech_config_ = SpeechConfig::FromSubscription(subscription_key_, region_);
        if (!speech_config_) {
            throw std::runtime_error("Failed to create SpeechConfig from subscription. Check key/region.");
        }
        // 기본 출력 오디오 포맷 설정 (예: 16kHz, 16bit, Mono PCM)
        // AvatarSync 및 WebRTC에서 일반적으로 사용되는 포맷
        speech_config_->SetSpeechSynthesisOutputFormat(SpeechSynthesisOutputFormat::Raw16Khz16BitMonoPcm);

    } catch (const std::exception& e) {
        std::cerr << "❌ Exception in AzureTTSEngine constructor: " << e.what() << std::endl;
        throw; // Rethrow to signal construction failure
    }
    std::cout << "  AzureTTSEngine initialized for region: " << region_ << std::endl;
}

AzureTTSEngine::~AzureTTSEngine() {
    std::cout << "ℹ️ Destroying AzureTTSEngine..." << std::endl;
    if (synthesis_active_.load() && synthesizer_) {
        std::cerr << "⚠️ WARNING: AzureTTSEngine destroyed while synthesis might be active. Attempting to stop..." << std::endl;
        try {
             // 비동기 중지 후 결과 기다림 (짧은 타임아웃)
            auto stop_future = synthesizer_->StopSpeakingAsync();
            stop_future.wait_for(std::chrono::seconds(2));
        } catch (const std::exception& e) {
            std::cerr << "   Exception during cleanup in destructor: " << e.what() << std::endl;
        }
    }
    synthesizer_.reset();
    speech_config_.reset();
    std::cout << "✅ AzureTTSEngine destroyed." << std::endl;
}

bool AzureTTSEngine::InitializeSynthesis(const SynthesisConfig& config) {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    if (synthesis_active_.load()) {
        std::cerr << "⚠️ InitializeSynthesis called while synthesis is active. Stop current synthesis first." << std::endl;
        return false;
    }
    current_synthesis_config_ = config;

    if (current_synthesis_config_.language_code().empty() || current_synthesis_config_.voice_name().empty()) {
        last_error_message_ = "Language code or voice name is empty in SynthesisConfig.";
        std::cerr << "❌ AzureTTSEngine: " << last_error_message_ << std::endl;
        return false;
    }

    try {
        speech_config_->SetSpeechSynthesisLanguage(current_synthesis_config_.language_code());
        speech_config_->SetSpeechSynthesisVoiceName(current_synthesis_config_.voice_name());

        // 이전 synthesizer_가 있다면 해제
        synthesizer_.reset();
        synthesizer_ = SpeechSynthesizer::FromConfig(speech_config_, nullptr); // 오디오 출력은 스트림으로 받음

        if (!synthesizer_) {
            throw std::runtime_error("Failed to create SpeechSynthesizer.");
        }

        // 이벤트 핸들러 연결
        synthesizer_->SynthesisStarted.Connect([this](const SpeechSynthesisEventArgs& e) { this->HandleSynthesisStarted(e); });
        synthesizer_->Synthesizing.Connect([this](const SpeechSynthesisEventArgs& e) { this->HandleSynthesizing(e); });
        synthesizer_->SynthesisCompleted.Connect([this](const SpeechSynthesisEventArgs& e) { this->HandleSynthesisCompleted(e); });
        synthesizer_->VisemeReceived.Connect([this](const SpeechSynthesisVisemeEventArgs& e) { this->HandleVisemeReceived(e); });

        synthesis_has_error_.store(false);
        last_error_message_.clear();
        std::cout << "   AzureTTSEngine initialized for synthesis. Lang: " << config.language_code()
                  << ", Voice: " << config.voice_name() << std::endl;
        return true;

    } catch (const std::exception& e) {
        last_error_message_ = "Exception during InitializeSynthesis: " + std::string(e.what());
        std::cerr << "❌ AzureTTSEngine: " << last_error_message_ << std::endl;
        synthesizer_.reset();
        return false;
    }
}

bool AzureTTSEngine::Synthesize(const std::string& text,
                                  const AudioVisemeCallback& audio_viseme_callback,
                                  const SynthesisCompletionCallback& completion_callback) {
    std::lock_guard<std::mutex> lock(engine_mutex_);

    if (synthesis_active_.load()) {
        last_error_message_ = "Synthesize called while another synthesis is already active.";
        std::cerr << "⚠️ AzureTTSEngine: " << last_error_message_ << std::endl;
        if(completion_callback) completion_callback(false, last_error_message_);
        return false;
    }
    if (!synthesizer_) {
        last_error_message_ = "Synthesizer not initialized. Call InitializeSynthesis first.";
        std::cerr << "❌ AzureTTSEngine: " << last_error_message_ << std::endl;
        if(completion_callback) completion_callback(false, last_error_message_);
        return false;
    }
     if (text.empty()) {
        last_error_message_ = "Input text for synthesis is empty.";
        std::cerr << "⚠️ AzureTTSEngine: " << last_error_message_ << std::endl;
        if(completion_callback) completion_callback(true, ""); // 빈 텍스트는 오류가 아닐 수 있음, 성공으로 처리하고 빈 오디오
        return true; // 또는 false로 처리하고 오류 메시지 전달
    }


    audio_viseme_callback_ = audio_viseme_callback;
    completion_callback_ = completion_callback;

    if (!audio_viseme_callback_ || !completion_callback_) {
        last_error_message_ = "Audio/Viseme or completion callback is null.";
        std::cerr << "❌ AzureTTSEngine: " << last_error_message_ << std::endl;
        // completion_callback_ 자체가 null일 수 있으므로 여기서 호출하지 않음
        return false;
    }

    synthesis_active_.store(true);
    synthesis_has_error_.store(false);
    last_error_message_.clear();

    std::cout << "⏳ AzureTTSEngine: Starting synthesis for text: \"" << text.substr(0, 50) << "...\"" << std::endl;

    // SSML 사용 여부 결정 (간단한 텍스트면 일반 텍스트, 아니면 SSML로 감싸기 등)
    // 여기서는 일반 텍스트 합성을 사용합니다. 복잡한 제어는 SSML 필요.
    // synthesizer_->SpeakTextAsync(text);
    // SSML 예시:
    // std::string ssml = "<speak version='1.0' xmlns='[http://www.w3.org/2001/10/synthesis](http://www.w3.org/2001/10/synthesis)' xml:lang='" + current_synthesis_config_.language_code() + "'>"
    //                  + "<voice name='" + current_synthesis_config_.voice_name() + "'>"
    //                  + text // SSML Injection에 취약할 수 있으므로 실제 사용 시 escape 처리 필요
    //                  + "</voice></speak>";
    // synthesizer_->SpeakSsmlAsync(ssml);

    // 비동기 합성 시작
    synthesizer_->SpeakTextAsync(text);

    return true; // 비동기 호출은 즉시 반환, 결과는 콜백으로 전달됨
}

void AzureTTSEngine::StopSynthesis() {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    if (!synthesis_active_.load() || !synthesizer_) {
        std::cout << "ℹ️ AzureTTSEngine: StopSynthesis called but not active or synthesizer is null." << std::endl;
        return;
    }
    std::cout << "⏳ AzureTTSEngine: Requesting to stop current synthesis..." << std::endl;
    synthesizer_->StopSpeakingAsync(); // 비동기 중지
    // 실제 중지는 SynthesisCanceled 또는 SynthesisCompleted 이벤트를 통해 확인됨
}

// --- Private Azure SDK Event Handlers ---

void AzureTTSEngine::HandleSynthesisStarted(const SpeechSynthesisEventArgs& e) {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    std::cout << "   AzureTTSEngine: Synthesis started. Stream ID: " << e.Result->ResultId << std::endl;
    // 필요시 초기 메타데이터 콜백
}

void AzureTTSEngine::HandleSynthesizing(const SpeechSynthesisEventArgs& e) {
    // 이 이벤트는 오디오 데이터를 포함
    std::vector<uint8_t> audio_chunk;
    auto audio_data = e.Result->GetAudioData();
    if (audio_data && audio_data->size() > 0) {
        audio_chunk.assign(audio_data->data(), audio_data->data() + audio_data->size());
    }

    // Viseme 데이터는 VisemeReceived 이벤트에서 별도로 처리되므로 여기서는 빈 벡터 전달
    std::vector<avatar_sync::VisemeData> empty_visemes;

    AudioVisemeCallback cb;
    {
      std::lock_guard<std::mutex> lock(engine_mutex_);
      cb = audio_viseme_callback_;
    }

    if (cb && !audio_chunk.empty()) {
        try {
            cb(audio_chunk, empty_visemes);
        } catch (const std::exception& ex) {
            std::cerr << "❌ Exception in audio_viseme_callback_ (Synthesizing): " << ex.what() << std::endl;
            // 에러 발생 시 합성 중단 고려
            synthesis_has_error_.store(true);
            last_error_message_ = "Exception in audio data callback.";
            synthesizer_->StopSpeakingAsync(); // 비동기 중단 요청
        }
    }
}

void AzureTTSEngine::HandleSynthesisCompleted(const SpeechSynthesisEventArgs& e) {
    std::vector<uint8_t> final_audio_chunk;
    auto audio_data = e.Result->GetAudioData(); // 마지막 오디오 데이터가 있을 수 있음
    if (audio_data && audio_data->size() > 0) {
        final_audio_chunk.assign(audio_data->data(), audio_data->data() + audio_data->size());
    }

    std::vector<avatar_sync::VisemeData> empty_visemes;
    AudioVisemeCallback audio_cb;
    SynthesisCompletionCallback completion_cb_local;
    bool error_occured_local;
    std::string error_msg_local;

    {
        std::lock_guard<std::mutex> lock(engine_mutex_);
        std::cout << "   AzureTTSEngine: Synthesis completed. Stream ID: " << e.Result->ResultId << std::endl;
        audio_cb = audio_viseme_callback_;
        completion_cb_local = completion_callback_;
        error_occured_local = synthesis_has_error_.load();
        error_msg_local = last_error_message_;

        synthesis_active_.store(false);
    }

    if (audio_cb && !final_audio_chunk.empty()) {
         try {
            audio_cb(final_audio_chunk, empty_visemes);
        } catch (const std::exception& ex) {
             std::cerr << "❌ Exception in audio_viseme_callback_ (Completion): " << ex.what() << std::endl;
             if (!error_occured_local) { // 콜백에서 오류가 발생했고, 이전에 다른 오류가 없었다면
                 error_occured_local = true;
                 error_msg_local = "Exception in final audio data callback.";
             }
        }
    }

    if (completion_cb_local) {
        try {
            completion_cb_local(!error_occured_local, error_msg_local);
        } catch (const std::exception& ex) {
            std::cerr << "❌ Exception in completion_callback_ (Completed): " << ex.what() << std::endl;
        }
    }
}

void AzureTTSEngine::HandleVisemeReceived(const SpeechSynthesisVisemeEventArgs& e) {
    // e.VisemeId, e.AudioOffset (100ns 단위), e.Animation (JSON 문자열)
    // VisemeId는 숫자형 문자열, Animation은 더 상세한 블렌드 쉐이프 정보 포함 가능
    std::vector<avatar_sync::VisemeData> visemes;
    avatar_sync::VisemeData viseme_data;

    viseme_data.set_viseme_id(std::to_string(e.VisemeId));

    // 오디오 오프셋 (100 나노초 단위)을 google.protobuf.Timestamp로 변환
    uint64_t offset_100ns = e.AudioOffset;
    *viseme_data.mutable_start_time() = ConvertTicksToTimestamp(offset_100ns);

    // Azure Viseme 이벤트는 발생 시점만 알려주고 지속 시간은 직접 명시하지 않음.
    // 다음 Viseme 이벤트와의 시간차 또는 오디오 청크 길이를 기반으로 추정하거나,
    // Avatar Sync에서 후처리하여 결정해야 할 수 있음.
    // 여기서는 간단히 매우 짧은 지속시간(0)으로 설정하거나, 또는 고정값을 사용.
    // 혹은 AvatarSync에서 이 정보를 어떻게 사용할지에 따라 다름.
    // 만약 AvatarSync가 연속된 viseme의 start_time을 보고 duration을 계산한다면 duration_sec는 덜 중요할 수 있음.
    viseme_data.set_duration_sec(0.05f); // 예시: 50ms (다음 viseme 발생 전까지 유지된다고 가정)

    visemes.push_back(viseme_data);

    //std::cout << "   Viseme: ID=" << e.VisemeId << ", Offset=" << e.AudioOffset / 10000 << "ms"
    //          << ", Animation: " << e.Animation << std::endl;


    AudioVisemeCallback cb;
    std::vector<uint8_t> empty_audio; // Viseme 이벤트는 오디오를 포함하지 않음
    {
      std::lock_guard<std::mutex> lock(engine_mutex_);
      cb = audio_viseme_callback_;
    }

    if (cb && !visemes.empty()) {
         try {
            cb(empty_audio, visemes);
        } catch (const std::exception& ex) {
            std::cerr << "❌ Exception in audio_viseme_callback_ (VisemeReceived): " << ex.what() << std::endl;
            // 에러 발생 시 합성 중단 고려
            synthesis_has_error_.store(true);
            last_error_message_ = "Exception in viseme data callback.";
            synthesizer_->StopSpeakingAsync();
        }
    }
}

google::protobuf::Timestamp AzureTTSEngine::ConvertTicksToTimestamp(uint64_t ticks_100ns) {
    google::protobuf::Timestamp ts;
    uint64_t seconds = ticks_100ns / 10000000; // 1초 = 10,000,000 * 100ns
    uint64_t remaining_ticks = ticks_100ns % 10000000;
    uint32_t nanos = static_cast<uint32_t>(remaining_ticks * 100); // 1 tick = 100ns
    ts.set_seconds(static_cast<int64_t>(seconds));
    ts.set_nanos(nanos);
    return ts;
}

} // namespace tts