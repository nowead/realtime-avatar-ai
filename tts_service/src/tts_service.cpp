// src/tts_service.cpp

#include "tts_service.h"
#include <iostream>

namespace tts {

TTSServiceImpl::TTSServiceImpl(AzureTTSClient* client)
  : tts_client_(client)
{}

grpc::Status TTSServiceImpl::Synthesize(
    grpc::ServerContext* context,
    const tts::TTSRequest* request,
    tts::TTSResponse* response
) {
  try {
    const auto& text   = request->text();
    const auto& voice  = request->voice();
    const auto& format = request->format();

    auto result = tts_client_->synthesize(text, voice, format);

    const auto& audio   = result.audio;
    const auto& visemes = result.visemes;
    if (audio.empty() || audio.size() > 20 * 1024 * 1024) {
      return grpc::Status(
        grpc::StatusCode::INTERNAL,
        "Invalid audio size"
      );
    }

    response->set_audio_data(audio.data(), audio.size());
    response->set_format(format);
    for (auto& v : visemes) {
      auto* e = response->add_visemes();
      e->set_id(v.first);
      e->set_time_ms(v.second);
    }
    return grpc::Status::OK;

  } catch (const std::exception& e) {
    return grpc::Status(
      grpc::StatusCode::INTERNAL,
      e.what()
    );
  }
}

} // namespace tts
