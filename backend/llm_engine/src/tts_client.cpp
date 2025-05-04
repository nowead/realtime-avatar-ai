#include "tts_client.h"
#include <iostream>
#include <stdexcept> // For runtime_error

namespace llm_engine {

TTSClient::TTSClient(const std::string& server_address)
  : server_address_(server_address) {
    try {
        channel_ = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
        if (!channel_) {
            throw std::runtime_error("Failed to create gRPC channel to TTS service at " + server_address);
        }
        stub_ = TTSService::NewStub(channel_);
        if (!stub_) {
            throw std::runtime_error("Failed to create TTSService::Stub.");
        }
    } catch (const std::exception& e) {
        std::cerr << "❌ Exception in TTSClient constructor: " << e.what() << std::endl;
        throw; // Re-throw
    }
    std::cout << "TTSClient initialized for address: " << server_address << std::endl;
}

TTSClient::~TTSClient() {
    std::cout << "ℹ️ Destroying TTSClient..." << std::endl;
    if (IsStreamActive()) {
        std::cerr << "⚠️ WARNING: TTSClient destroyed while stream was active for session ["
                  << session_id_ << "]. Attempting to finish stream..." << std::endl;
        try {
            FinishStream(); // Ignore status, just try to clean up
        } catch (const std::exception& e) {
            std::cerr << "   Exception during TTSClient cleanup in destructor: " << e.what() << std::endl;
        }
    }
    stub_.reset();
    channel_.reset();
    std::cout << "✅ TTSClient destroyed." << std::endl;
}

bool TTSClient::StartStream(const std::string& session_id, const std::string& language_code, const std::string& voice_name) {
    std::lock_guard<std::mutex> lock(stream_mutex_);

    if (stream_active_.load()) {
        std::cerr << "⚠️ TTS Client: StartStream called while another stream [" << session_id_ << "] is already active." << std::endl;
        return false;
    }
     if (session_id.empty() || language_code.empty() || voice_name.empty()) {
         std::cerr << "❌ TTS Client: StartStream called with empty session_id, language_code, or voice_name." << std::endl;
         return false;
     }

    session_id_ = session_id; // Store for logging
    std::cout << "⏳ TTS Client: Starting stream for session [" << session_id_ << "]..." << std::endl;

    context_ = std::make_unique<ClientContext>();
    // Set a deadline if needed:
    // std::chrono::system_clock::time_point deadline = std::chrono::system_clock::now() + std::chrono::seconds(60);
    // context_->set_deadline(deadline);

    stream_ = stub_->SynthesizeStream(context_.get(), &server_response_);

    if (!stream_) {
        std::cerr << "❌ TTS Client: Failed to initiate gRPC stream to TTS engine for session [" << session_id_ << "]." << std::endl;
        context_.reset(); // Clean up context
        session_id_.clear();
        return false;
    }

    // Send the initial SynthesisConfig message
    std::cout << "   TTS Client: Sending initial SynthesisConfig for session [" << session_id_ << "]..." << std::endl;
    TTSStreamRequest config_request;
    SynthesisConfig* config = config_request.mutable_config();
    config->set_session_id(session_id_);
    config->set_language_code(language_code);
    config->set_voice_name(voice_name);
    // Set other config fields if necessary

    if (!stream_->Write(config_request)) {
        std::cerr << "❌ TTS Client: Failed to write initial SynthesisConfig for session [" << session_id_ << "]. Finishing stream." << std::endl;
        // Attempt to finish the stream immediately on write failure
        stream_->Finish(); // Get status, but mainly signal end
        stream_.reset();  // Release stream resources
        context_.reset(); // Release context resources
        session_id_.clear();
        stream_active_.store(false); // Mark as inactive
        return false;
    }

    stream_active_.store(true); // Mark stream as active *after* successful config write
    std::cout << "✅ TTS Client: Stream successfully started and SynthesisConfig sent for session [" << session_id_ << "]." << std::endl;
    return true;
}

bool TTSClient::SendTextChunk(const std::string& text) {
    if (!IsStreamActive()) { // Check activity status first
        std::cerr << "⚠️ TTS Client: SendTextChunk called but stream is not active for session [" << session_id_ << "]." << std::endl;
        return false;
    }

    TTSStreamRequest request;
    request.set_text_chunk(text); // Set the text_chunk field

    bool write_ok = false;
    {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        // Check stream validity again inside the lock
        if (stream_) {
             // std::cout << "   TTS Client: Sending chunk (session=" << session_id_ << "): '" << text.substr(0, 50) << "...'" << std::endl;
            write_ok = stream_->Write(request);
        } else {
            // Stream might have been closed between IsStreamActive check and here
            write_ok = false;
            std::cerr << "⚠️ TTS Client: Stream became invalid before writing chunk for session [" << session_id_ << "]." << std::endl;
        }
    } // Lock released here

    if (!write_ok) {
        std::cerr << "❌ TTS Client: Failed to write text chunk to TTS engine stream for session [" << session_id_ << "]. Marking as inactive." << std::endl;
        // Don't reset stream_ here, let FinishStream handle the final state retrieval
        stream_active_.store(false); // Mark as inactive on write failure
        return false;
    }
    return true;
}

Status TTSClient::FinishStream() {
    std::lock_guard<std::mutex> lock(stream_mutex_);

    if (!stream_active_.load() || !stream_) {
        std::cerr << "⚠️ TTS Client: FinishStream called but stream is not active or already finished for session [" << session_id_ << "]." << std::endl;
         // If stream is already null, assume it was finished or failed earlier.
         // If only inactive, return FAILED_PRECONDITION.
         return stream_ ? Status(grpc::StatusCode::FAILED_PRECONDITION, "Stream not active")
                       : Status(grpc::StatusCode::OK, "Stream already finished or failed"); // Or choose appropriate error
    }

    std::cout << "⏳ TTS Client: Finishing stream for session [" << session_id_ << "]..." << std::endl;

    // Signal that no more messages will be sent
    bool writes_done_ok = stream_->WritesDone();
    if (!writes_done_ok) {
        // This might happen if the stream was already broken (e.g., server died)
        std::cerr << "⚠️ TTS Client: WritesDone failed on TTS stream for session [" << session_id_ << "]. Stream might be broken." << std::endl;
        // Proceed to Finish() anyway to get the final status
    } else {
        std::cout << "   TTS Client: WritesDone called successfully for session [" << session_id_ << "]." << std::endl;
    }

    // Wait for the server to process all messages and return the final status
    std::cout << "   TTS Client: Waiting for final status (Finish) from TTS server for session [" << session_id_ << "]..." << std::endl;
    Status status = stream_->Finish();

    // Clean up resources associated with this stream
    stream_.reset();
    context_.reset();
    stream_active_.store(false);
    std::string finished_session_id = session_id_; // Copy before clearing
    session_id_.clear();


    if (status.ok()) {
        std::cout << "✅ TTS Client: Stream finished successfully for session [" << finished_session_id << "]. Server returned Empty." << std::endl;
    } else {
        std::cerr << "❌ TTS Client: Stream finished with error for session [" << finished_session_id
                  << "]. Status: (" << status.error_code() << "): " << status.error_message() << std::endl;
    }

    return status; // Return the final status
}

bool TTSClient::IsStreamActive() const {
    std::lock_guard<std::mutex> lock(stream_mutex_);
    // Stream is active if the flag is set AND the stream pointer is valid
    return stream_active_.load() && (stream_ != nullptr);
}

} // namespace llm_engine