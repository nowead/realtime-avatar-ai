#pragma once

#include <grpcpp/grpcpp.h>
#include "avatar_sync.grpc.pb.h"
#include <functional>
#include <thread>
#include <atomic>

class TtsClient {
public:
    using AudioCallback = std::function<
        void(const void* audio_data,
             int bits_per_sample,
             int sample_rate,
             size_t channels,
             size_t frames)>;
    using VisemeCallback = std::function<
        void(const avatar_sync::VisemeData& viseme)>;

    explicit TtsClient(std::shared_ptr<grpc::Channel> channel)
        : stub_(avatar_sync::AvatarSyncService::NewStub(channel)),
          running_(false) {}

    // Start streaming: sends config, then begins reading responses
    void Start(const std::string& session_id,
               AudioCallback on_audio,
               VisemeCallback on_viseme) {
        Stop();  // in case already running
        context_ = std::make_unique<grpc::ClientContext>();
        avatar_sync::SyncConfig config;
        config.set_session_id(session_id);

        // Initiate RPC (assume server-streaming)
        reader_ = stub_->SyncAvatarStream(context_.get(), config);
        running_ = true;

        // Spawn reader thread
        reader_thread_ = std::thread([this, on_audio, on_viseme]() {
            avatar_sync::AvatarSyncStreamRequest msg;
            while (running_ && reader_->Read(&msg)) {
                switch (msg.request_data_case()) {
                    case avatar_sync::AvatarSyncStreamRequest::kAudioChunk: {
                        const auto& chunk = msg.audio_chunk();
                        // Assuming 16-bit PCM
                        on_audio(
                            chunk.data(),
                            16,
                            /*sample_rate*/ 24000,
                            /*channels*/ 1,
                            chunk.size() / (2 * 1));
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

    // Stop streaming and join thread
    void Stop() {
        running_ = false;
        if (context_) context_->TryCancel();
        if (reader_thread_.joinable()) reader_thread_.join();
        reader_.reset();
        context_.reset();
    }

    ~TtsClient() {
        Stop();
    }

private:
    std::unique_ptr<avatar_sync::AvatarSyncService::Stub> stub_;
    std::unique_ptr<grpc::ClientContext> context_;
    std::unique_ptr<grpc::ClientReader<avatar_sync::AvatarSyncStreamRequest>> reader_;
    std::thread reader_thread_;
    std::atomic<bool> running_;
};