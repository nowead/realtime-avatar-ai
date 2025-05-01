#include "tts_service.h"
#include <iostream>
#include <string>
#include <vector>
#include <future>
#include <thread> // std::thread 추가

namespace tts {

TTSServiceImpl::TTSServiceImpl(AzureTTSClient* client)
  : tts_client_(client)
{}

grpc::Status TTSServiceImpl::SynthesizeStream(
    grpc::ServerContext* context,
    grpc::ServerReaderWriter<tts::TTSStreamResponse, tts::TTSStreamRequest>* stream
) {
    std::cout << "✅ New client connected for streaming TTS." << std::endl;

    tts::TTSStreamRequest request;
    std::string text_to_synthesize;
    std::string voice_name;
    bool config_received = false;

    while (stream->Read(&request)) {
        // --- oneof 필드 확인 방식 변경 ---
        switch (request.request_data_case()) {
            case tts::TTSStreamRequest::kConfig:
                if (!config_received) {
                    voice_name = request.config().voice();
                    config_received = true;
                    std::cout << "🎤 Config received: Voice=" << voice_name << std::endl;
                } else {
                    std::cerr << "⚠️ Config message received more than once." << std::endl;
                }
                break;
            case tts::TTSStreamRequest::kTextChunk:
                if (!config_received) {
                    std::cerr << "⚠️ Text chunk received before config. Using default." << std::endl;
                    voice_name = "ko-KR-SoonBokNeural"; // 기본값 사용
                    config_received = true;
                    std::cout << "🎤 Using default config: Voice=" << voice_name << std::endl;
                }
                text_to_synthesize += request.text_chunk();
                break;
            case tts::TTSStreamRequest::REQUEST_DATA_NOT_SET:
            default:
                 std::cerr << "⚠️ Received request with no data set." << std::endl;
                 break;
        }
        // --- oneof 필드 확인 방식 변경 끝 ---
    }
     std::cout << "🏁 Client finished sending text. Total length: " << text_to_synthesize.length() << " chars." << std::endl;

    if (!config_received || text_to_synthesize.empty()) {
         std::cerr << "❌ No config or text received." << std::endl;
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Config and text required.");
    }

    std::promise<grpc::Status> final_status_promise;
    auto final_status_future = final_status_promise.get_future();

    auto audio_callback = [&](const uint8_t* data, size_t size) {
        tts::TTSStreamResponse response;
        response.set_audio_chunk(data, size);
        std::lock_guard<std::mutex> lock(stream_mutex_);
        if (!stream->Write(response)) {
            std::cerr << "❌ Failed to write audio chunk to stream." << std::endl;
            // Consider setting promise with error here
            // final_status_promise.set_value(grpc::Status(grpc::StatusCode::UNAVAILABLE, "Stream write failed"));
        }
    };

    auto viseme_callback = [&](int id, uint64_t offset_ms) {
        tts::TTSStreamResponse response;
        auto* viseme_event = response.mutable_viseme();
        viseme_event->set_id(id);
        viseme_event->set_time_ms(offset_ms);
        std::lock_guard<std::mutex> lock(stream_mutex_);
        if (!stream->Write(response)) {
            std::cerr << "❌ Failed to write viseme event to stream." << std::endl;
             // Consider setting promise with error here
             // final_status_promise.set_value(grpc::Status(grpc::StatusCode::UNAVAILABLE, "Stream write failed"));
        }
    };

    auto completion_callback = [&](bool success, const std::string& error_msg) {
        // Use try_set_value to avoid exception if promise is already set by write failure
        if (success) {
            std::cout << "✅ Azure TTS synthesis completed successfully." << std::endl;
            try { final_status_promise.set_value(grpc::Status::OK); } catch (const std::future_error& e) {}
        } else {
            std::cerr << "❌ Azure TTS synthesis failed: " << error_msg << std::endl;
             try { final_status_promise.set_value(grpc::Status(grpc::StatusCode::INTERNAL, "TTS synthesis failed: " + error_msg)); } catch (const std::future_error& e) {}
        }
    };

    std::thread tts_thread([this, text = std::move(text_to_synthesize), voice = std::move(voice_name), audio_callback, viseme_callback, completion_callback]() {
         tts_client_->synthesizeStream(text, voice, audio_callback, viseme_callback, completion_callback);
    });
    tts_thread.detach();

    grpc::Status final_status = final_status_future.get();

    std::cout << "🏁 Stream finished. Returning status: " << (final_status.ok() ? "OK" : final_status.error_message()) << std::endl;
    return final_status;
}

} // namespace tts