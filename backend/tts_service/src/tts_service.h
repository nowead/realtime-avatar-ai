#pragma once

#include "tts.pb.h"          // for tts::TTSRequest, tts::TTSResponse
#include "tts.grpc.pb.h"     // for tts::TTSService::Service
#include "azure_tts_client.h"
#include <grpcpp/grpcpp.h>

namespace tts {

class TTSServiceImpl final : public TTSService::Service {
public:
  explicit TTSServiceImpl(AzureTTSClient* client);

  // unary RPC
  grpc::Status Synthesize(
    grpc::ServerContext* context,
    const tts::TTSRequest* request,
    tts::TTSResponse* response
  ) override;

private:
  AzureTTSClient* tts_client_;
};

} // namespace tts
