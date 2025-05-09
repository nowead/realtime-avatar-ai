#pragma once

#include <grpcpp/grpcpp.h>
#include "stt.grpc.pb.h"
#include <mutex>
#include <unordered_map>

class SttClient {
public:
    explicit SttClient(std::shared_ptr<grpc::Channel> channel)
        : stub_(stt::STTService::NewStub(channel)) {}

    // 시작: 세션별 스트림 생성
    void StartStream(const std::string& session_id) {
        std::lock_guard<std::mutex> lock(mu_);
        grpc::ClientContext context;
        auto writer = stub_->RecognizeStream(&context, &responses_[session_id]);
        writers_[session_id] = std::move(writer);
    }

    // 오디오 청크 전송
    void SendAudioChunk(const std::string& session_id,
                        const void* data,
                        int bits_per_sample,
                        int sample_rate,
                        size_t channels,
                        size_t frames) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = writers_.find(session_id);
        if (it == writers_.end()) return;
        stt::STTStreamRequest req;
        if (!sent_config_[session_id]) {
            auto* cfg = req.mutable_config();
            cfg->set_session_id(session_id);
            // set additional fields if needed
            sent_config_[session_id] = true;
        }
        req.set_audio_chunk(data, frames * channels * (bits_per_sample/8));
        it->second->Write(req);
    }

    // 종료: 스트림 닫기
    void FinishStream(const std::string& session_id) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = writers_.find(session_id);
        if (it != writers_.end()) {
            it->second->WritesDone();
            it->second->Finish();
            writers_.erase(it);
            responses_.erase(session_id);
            sent_config_.erase(session_id);
        }
    }

private:
    std::unique_ptr<stt::STTService::Stub> stub_;
    std::mutex mu_;
    std::unordered_map<std::string, std::unique_ptr<grpc::ClientWriter<stt::STTStreamRequest>>> writers_;
    std::unordered_map<std::string, grpc::ClientReader<google::protobuf::Empty>> responses_;
    std::unordered_map<std::string, bool> sent_config_;
};