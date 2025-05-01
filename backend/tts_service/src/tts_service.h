#pragma once

#include "tts.pb.h"          // 생성된 Protobuf 메시지 헤더
#include "tts.grpc.pb.h"     // 생성된 gRPC 서비스 헤더
#include "azure_tts_client.h"
#include <grpcpp/grpcpp.h>
#include <mutex> // for std::mutex

namespace tts {

// TTSService::Service 상속 (tts.grpc.pb.h 에 정의됨)
class TTSServiceImpl final : public TTSService::Service {
public:
  explicit TTSServiceImpl(AzureTTSClient* client);

  // 새로운 Bi-directional Streaming RPC
  grpc::Status SynthesizeStream(
    grpc::ServerContext* context,
    grpc::ServerReaderWriter<tts::TTSStreamResponse, tts::TTSStreamRequest>* stream
  ) override;


private:
  AzureTTSClient* tts_client_;
  // gRPC 스트림 쓰기 작업을 보호하기 위한 뮤텍스 (Azure SDK 콜백은 다른 스레드에서 실행될 수 있음)
  std::mutex stream_mutex_;
};

} // namespace tts