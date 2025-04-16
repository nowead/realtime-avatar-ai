#include "tts_service.h"
#include "tts_engine.h"

using grpc::Status;
using grpc::ServerContext;
using tts::TTSRequest;
using tts::TTSResponse;

namespace tts {

Status TTSServiceImpl::Synthesize(ServerContext* context,
                                  const TTSRequest* request,
                                  TTSResponse* response) {
    std::string text = request->text();
    std::string voice = request->voice();

    std::vector<uint8_t> audio = run_open_tts_to_memory(text, voice);  // ✅ 수정된 함수 사용

    if (audio.empty()) {
        return Status(grpc::StatusCode::INTERNAL, "TTS 변환 실패: audio 데이터가 비어 있음");
    }

    response->set_audio_data(audio.data(), audio.size());
    response->set_format("wav");

    return Status::OK;
}

} // namespace tts
