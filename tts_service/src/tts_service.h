#ifndef TTS_SERVICE_H
#define TTS_SERVICE_H

#include "tts.grpc.pb.h"

namespace tts {

// gRPC TTSService 정의를 상속하여 서비스 로직을 구현
class TTSServiceImpl final : public TTSService::Service {
public:
    grpc::Status Synthesize(grpc::ServerContext* context,
                            const TTSRequest* request,
                            TTSResponse* response) override;
};

} // namespace tts

#endif // TTS_SERVICE_H
