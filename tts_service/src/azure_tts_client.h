#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <utility>

namespace tts {

  // Viseme 정보를 담는 별칭 (ID, time_ms)
  using VisemeInfo = std::pair<int, uint64_t>;

  struct SynthesisResult {
    std::vector<uint8_t> audio;
    std::vector<VisemeInfo> visemes;
  };

  class AzureTTSClient {
  public:
    AzureTTSClient(const std::string& key, const std::string& region);
    SynthesisResult synthesize(const std::string& text,
                               const std::string& voice,
                               const std::string& format);
  private:
    std::string subscription_key_;
    std::string region_;
  };

} // namespace tts
