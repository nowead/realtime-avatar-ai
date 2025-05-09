#include "TtsClient.h"
#include <iostream>

void TtsClient::Start(const std::string& session_id,
                      AudioCallback on_audio,
                      VisemeCallback on_viseme) {
    // 기존 스트림 정리
    Stop();

    // 새로운 RPC 컨텍스트 생성 및 시작
    context_ = std::make_unique<grpc::ClientContext>();
    avatar_sync::SyncConfig config;
    config.set_session_id(session_id);

    reader_ = stub_->SyncAvatarStream(context_.get(), config);
    running_ = true;

    // 서버 스트리밍 수신 스레드
    reader_thread_ = std::thread([this, on_audio, on_viseme]() {
        avatar_sync::AvatarSyncStreamRequest msg;
        while (running_ && reader_->Read(&msg)) {
            switch (msg.request_data_case()) {
                case avatar_sync::AvatarSyncStreamRequest::kAudioChunk: {
                    const auto& chunk = msg.audio_chunk();
                    // 16-bit PCM, mono, 24kHz 가정
                    on_audio(
                        chunk.data(),
                        16,
                        24000,
                        1,
                        chunk.size() / (2 * 1)
                    );
                    break;
                }
                case avatar_sync::AvatarSyncStreamRequest::kVisemeData: {
                    on_viseme(msg.viseme_data());
                    break;
                }
                default:
                    break;
            }
        }
        reader_->Finish();
    });
}