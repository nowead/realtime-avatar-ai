#include "azure_tts_client.h"
#include <speechapi_cxx.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <stdexcept>
#include <vector>
#include <iostream>

using namespace Microsoft::CognitiveServices::Speech;
using namespace Microsoft::CognitiveServices::Speech::Audio;

namespace tts {

// 생성자
AzureTTSClient::AzureTTSClient(const std::string& key,
                               const std::string& region)
  : subscription_key_(key),
    region_(region)
{}

// synthesize 구현
SynthesisResult AzureTTSClient::synthesize(
    const std::string& text,
    const std::string& voice,
    const std::string& format
) {
    // Config 설정
    auto config = SpeechConfig::FromSubscription(subscription_key_, region_);
    config->SetSpeechSynthesisOutputFormat(
      SpeechSynthesisOutputFormat::Raw16Khz16BitMonoPcm);
    if (!voice.empty()) {
        config->SetSpeechSynthesisVoiceName(voice);
        if (voice.find("Neural") == std::string::npos ||
            voice.rfind("en-", 0) != 0)
        {
            std::cerr << "⚠️ Voice may not support Viseme output: "
                      << voice << "\n";
        }
    }

    // Synthesizer 생성
    auto synthesizer = SpeechSynthesizer::FromConfig(config);

    // VisemeInfo 수집 + 완료 대기
    std::queue<VisemeInfo> viseme_queue;
    std::mutex viseme_mutex;
    std::condition_variable cv;
    std::mutex cv_mutex;
    bool done = false;

    synthesizer->VisemeReceived.Connect(
      [&viseme_queue, &viseme_mutex](const SpeechSynthesisVisemeEventArgs& e) {
        std::lock_guard<std::mutex> lk(viseme_mutex);
        viseme_queue.push({
          static_cast<int>(e.VisemeId),
          static_cast<uint64_t>(e.AudioOffset / 10000)  // ms
        });
      }
    );
    synthesizer->SynthesisCompleted.Connect(
      [&cv, &cv_mutex, &done](const SpeechSynthesisEventArgs&) {
        {
          std::lock_guard<std::mutex> lk(cv_mutex);
          done = true;
        }
        cv.notify_one();
      }
    );

    // SSML 구성 (viseme 요청 포함)
    std::string ssml =
      "<speak version='1.0' "
        "xmlns='http://www.w3.org/2001/10/synthesis' "
        "xmlns:mstts='http://www.w3.org/2001/mstts' "
        "xml:lang='en-US'>"
        "<voice name='" + voice + "'>"
          "<mstts:viseme type='redlips_front'/>"
          "<prosody rate='0%' pitch='50%'>" + text +
          "</prosody>"
        "</voice>"
      "</speak>";

    // 합성 시작
    auto result = synthesizer->StartSpeakingSsmlAsync(ssml).get();
    if (result->Reason != ResultReason::SynthesizingAudioStarted) {
      throw std::runtime_error(
        "TTS start failed, Reason="
        + std::to_string(static_cast<int>(result->Reason))
      );
    }

    // AudioDataStream에서 오디오 읽기
    auto audio_stream = AudioDataStream::FromResult(result);
    std::vector<uint8_t> audio;
    std::vector<uint8_t> buf(4096);
    uint32_t read = 0;
    while ((read = audio_stream->ReadData(buf.data(), buf.size())) > 0) {
      audio.insert(audio.end(), buf.begin(), buf.begin() + read);
    }

    // 합성 완료 대기
    {
      std::unique_lock<std::mutex> lk(cv_mutex);
      cv.wait(lk, [&]{ return done; });
    }

    // VisemeInfo 벡터로 추출
    std::vector<VisemeInfo> visemes;
    {
      std::lock_guard<std::mutex> lk(viseme_mutex);
      while (!viseme_queue.empty()) {
        visemes.push_back(viseme_queue.front());
        viseme_queue.pop();
      }
    }

    // 디버그용 로그
    for (auto& v : visemes) {
      std::cout << "🗣️ VisemeLog: ID=" << v.first
                << ", Time=" << v.second << "ms\n";
    }

    return { std::move(audio), std::move(visemes) };
}

} // namespace tts
