#include "llm_service.h"
#include <iostream>
#include <string>
#include <vector>
#include <random> // For UUID
#include <sstream> // For UUID
#include <iomanip> // For UUID
#include <thread>  // For sleep_for, potentially
#include <chrono>
#include <future>  // For promise/future

// Required for gRPC status codes
using grpc::StatusCode;

namespace llm_engine {

// Simple UUID generation (reuse or improve as needed)
std::string LLMServiceImpl::generate_uuid() {
     std::random_device rd;
     std::mt19937_64 gen(rd());
     std::uniform_int_distribution<uint64_t> dis;
     std::stringstream ss;
     ss << std::hex << std::setfill('0');
     ss << std::setw(16) << dis(gen) << std::setw(16) << dis(gen);
     return ss.str();
}


LLMServiceImpl::LLMServiceImpl(std::shared_ptr<TTSClient> tts_client,
                                 std::shared_ptr<OpenAIClient> openai_client)
    : tts_client_(tts_client), openai_client_(openai_client) {
    if (!tts_client_) {
        throw std::runtime_error("TTSClient cannot be null in LLMServiceImpl.");
    }
    if (!openai_client_) {
        throw std::runtime_error("OpenAIClient cannot be null in LLMServiceImpl.");
    }
    std::cout << "LLMServiceImpl initialized." << std::endl;
}


// --- OpenAI Callback Handlers ---

// Called by OpenAIClient when a text chunk is available
void LLMServiceImpl::handle_openai_chunk(const std::string& session_id, const std::string& chunk, std::atomic<bool>& tts_stream_ok) {
    if (!tts_stream_ok.load()) {
         // std::cout << "   [" << session_id << "] OpenAI chunk received, but TTS stream has error. Skipping send." << std::endl;
         return; // Don't send if TTS stream failed
    }
    // std::cout << "   [" << session_id << "] OpenAI -> TTS: '" << chunk << "'" << std::endl;
    if (!tts_client_->SendTextChunk(chunk)) {
        std::cerr << "❌ [" << session_id << "] Failed to send chunk to TTS service. Marking TTS stream as failed." << std::endl;
        tts_stream_ok.store(false); // Mark TTS stream as having failed
    }
}

// Called by OpenAIClient when the stream ends or errors out
void LLMServiceImpl::handle_openai_completion(
    const std::string& session_id,
    bool success,
    const std::string& error_message,
    std::promise<void>& openai_done_promise,
    std::atomic<bool>& overall_success,
    std::string& last_error)
{
     std::cout << "🏁 [" << session_id << "] OpenAI stream completion. Success: " << success << std::endl;
     if (!success) {
        std::cerr << "   OpenAI Error: " << error_message << std::endl;
        // Only set error state if not already set by another failure
        bool expected = true;
        if(overall_success.compare_exchange_strong(expected, false)) {
             last_error = "OpenAI streaming failed: " + error_message;
        }
     }
     // Signal that OpenAI processing is done, regardless of success/failure
     try {
         openai_done_promise.set_value();
     } catch (const std::future_error& e) {
          std::cerr << "ℹ️ [" << session_id << "] OpenAI promise already set in completion handler: " << e.what() << std::endl;
     }
}

// --- Main RPC Implementation ---

Status LLMServiceImpl::ProcessTextStream(
    ServerContext* context,
    ServerReader<LLMStreamRequest>* reader,
    Empty* response)
{
    const std::string client_peer = context->peer();
    const std::string session_id = generate_uuid(); // Generate unique ID for this call
    std::cout << "✅ [" << session_id << "] New LLM stream connection from: " << client_peer << std::endl;

    std::string stt_session_id; // The session ID provided by the STT client
    std::vector<ChatMessage> chat_history; // Store conversation for OpenAI

    std::atomic<bool> tts_stream_started{false};
    std::atomic<bool> tts_stream_ok{true}; // Tracks if TTS sending is still okay
    std::atomic<bool> openai_processing_started{false};
    std::atomic<bool> overall_success{true}; // Tracks overall success of the stream
    std::string last_error_message;

    std::promise<void> openai_done_promise;
    auto openai_done_future = openai_done_promise.get_future();

    // --- Graceful Cleanup Lambda ---
    auto cleanup_resources = [&](bool finish_tts) {
        std::cout << "🧹 [" << session_id << "] Cleaning up LLM resources... FinishTTS=" << finish_tts << std::endl;
        if (finish_tts && tts_client_ && tts_stream_started.load()) {
            std::cout << "   Finishing TTS stream..." << std::endl;
            Status tts_status = tts_client_->FinishStream();
            if (!tts_status.ok()) {
                std::cerr << "   ⚠️ TTS stream finish error during cleanup: ("
                          << tts_status.error_code() << ") " << tts_status.error_message() << std::endl;
                // Update overall status if needed, but avoid overwriting earlier critical error
                 bool expected = true;
                 if (overall_success.compare_exchange_strong(expected, false)) {
                      last_error_message = "TTS stream finish error: " + tts_status.error_message();
                 }
            } else {
                std::cout << "   TTS stream finished successfully during cleanup." << std::endl;
            }
            tts_stream_started.store(false); // Mark as finished
        }
         // Ensure OpenAI promise is fulfilled if cleanup happens before completion
         try { openai_done_promise.set_value(); } catch (const std::future_error&) {}
    };


    try {
        // 1. Read Initial Config Message from STT Client
        LLMStreamRequest initial_request;
        std::cout << "   [" << session_id << "] Waiting for initial SessionConfig from client..." << std::endl;
        if (!reader->Read(&initial_request)) {
            last_error_message = "Failed to read initial request from client.";
            std::cerr << "❌ [" << session_id << "] " << last_error_message << " Peer: " << client_peer << std::endl;
            overall_success.store(false);
            return Status(StatusCode::INVALID_ARGUMENT, last_error_message);
        }

        if (initial_request.request_data_case() != LLMStreamRequest::kConfig) {
            last_error_message = "Initial request must be SessionConfig.";
            std::cerr << "❌ [" << session_id << "] " << last_error_message << std::endl;
            overall_success.store(false);
            return Status(StatusCode::INVALID_ARGUMENT, last_error_message);
        }

        stt_session_id = initial_request.config().session_id();
        if (stt_session_id.empty()) {
             last_error_message = "Session ID is missing in SessionConfig.";
             std::cerr << "❌ [" << session_id << "] " << last_error_message << std::endl;
             overall_success.store(false);
             return Status(StatusCode::INVALID_ARGUMENT, last_error_message);
        }
        std::cout << "   [" << session_id << "] Config received: STT Session ID = " << stt_session_id << std::endl;

        // TODO: Extract language, voice etc. from config if needed for TTS/OpenAI
         std::string tts_language = "ko-KR"; // Example: Hardcoded or from config
         std::string tts_voice = "default_voice"; // Example: Hardcoded or from config
         std::string system_prompt = "You are a helpful assistant."; // Example

         // Add system prompt to chat history
         chat_history.push_back({"system", system_prompt});


        // 2. Start Stream to TTS Service
        std::cout << "   [" << session_id << "] Starting stream to TTS Service..." << std::endl;
        if (!tts_client_->StartStream(session_id, tts_language, tts_voice)) { // Use LLM's session_id for TTS
            last_error_message = "Failed to start stream to TTS Service.";
            std::cerr << "❌ [" << session_id << "] " << last_error_message << std::endl;
            overall_success.store(false);
            // No need to call cleanup here, TTS stream didn't even start
            return Status(StatusCode::INTERNAL, last_error_message);
        }
        tts_stream_started.store(true);
        tts_stream_ok.store(true); // Mark TTS stream as initially OK
        std::cout << "   [" << session_id << "] TTS stream started successfully." << std::endl;


        // 3. Read Text Chunks from STT Client & Accumulate
         std::string accumulated_text;
         LLMStreamRequest chunk_request;
         std::cout << "   [" << session_id << "] Waiting for text chunks from client..." << std::endl;

        while (overall_success.load() && !context->IsCancelled() && reader->Read(&chunk_request)) {
            if (chunk_request.request_data_case() == LLMStreamRequest::kTextChunk) {
                const std::string& chunk = chunk_request.text_chunk();
                 if (!chunk.empty()) {
                     // std::cout << "   [" << session_id << "] Received Text Chunk: '" << chunk << "'" << std::endl;
                     accumulated_text += chunk;
                     // Optional: Add partial text to history immediately or wait for full turn?
                     // For simplicity, we'll send the whole accumulated text later.
                 }
            } else if (chunk_request.request_data_case() == LLMStreamRequest::kConfig) {
                 std::cerr << "⚠️ [" << session_id << "] Received unexpected Config message after initialization. Ignoring." << std::endl;
            } else {
                 std::cerr << "⚠️ [" << session_id << "] Received message with unexpected type. Ignoring." << std::endl;
            }
        } // End of reader loop


        // Check loop exit reason
        if (context->IsCancelled()) {
             std::cout << "ℹ️ [" << session_id << "] Client cancelled the request." << std::endl;
             last_error_message = "Request cancelled by client.";
             overall_success.store(false);
             cleanup_resources(true); // Try to clean up TTS stream
             return Status(StatusCode::CANCELLED, last_error_message);
        }
        if (!overall_success.load()) {
             std::cerr << "❌ [" << session_id << "] Error occurred during processing. Exiting read loop. Reason: " << last_error_message << std::endl;
             cleanup_resources(tts_stream_started.load()); // Clean up if started
             return Status(StatusCode::INTERNAL, "Internal error during stream processing: " + last_error_message);
        }

        std::cout << "ℹ️ [" << session_id << "] Client finished sending text chunks." << std::endl;
        std::cout << "   [" << session_id << "] Accumulated Text: '" << accumulated_text << "'" << std::endl;

         // Add the fully accumulated user text to chat history
         if (!accumulated_text.empty()) {
             chat_history.push_back({"user", accumulated_text});
         } else {
              std::cout << "   [" << session_id << "] No text accumulated from client. Skipping OpenAI call." << std::endl;
              cleanup_resources(true); // Finish TTS stream
              return Status::OK; // Nothing to process
         }


        // 4. Start Asynchronous OpenAI Streaming Call
         std::cout << "   [" << session_id << "] Starting OpenAI streaming processing..." << std::endl;
         openai_processing_started.store(true);

        // Define the callbacks using lambdas capturing necessary context
        auto openai_chunk_cb = [this, session_id, &tts_stream_ok](const std::string& chunk) {
            this->handle_openai_chunk(session_id, chunk, tts_stream_ok);
        };
        auto openai_completion_cb = [this, session_id, &openai_done_promise, &overall_success, &last_error_message](bool success, const std::string& error_msg) {
            this->handle_openai_completion(session_id, success, error_msg, openai_done_promise, overall_success, last_error_message);
        };

        try {
            openai_client_->StreamChatCompletion(chat_history, openai_chunk_cb, openai_completion_cb);
        } catch (const std::exception& e) {
            last_error_message = "Failed to start OpenAI streaming: " + std::string(e.what());
            std::cerr << "❌ [" << session_id << "] " << last_error_message << std::endl;
            overall_success.store(false);
            openai_processing_started.store(false);
             try { openai_done_promise.set_value(); } catch(...) {} // Fulfill promise on exception
             cleanup_resources(tts_stream_started.load());
             return Status(StatusCode::INTERNAL, last_error_message);
        }


        // 5. Wait for OpenAI processing to complete
        std::cout << "   [" << session_id << "] Waiting for OpenAI processing to complete..." << std::endl;
        // Add a timeout to avoid waiting indefinitely
        std::future_status wait_status = openai_done_future.wait_for(std::chrono::seconds(60)); // 60 second timeout

        if (wait_status == std::future_status::timeout) {
             std::cerr << "❌ [" << session_id << "] Timed out waiting for OpenAI completion (60s)." << std::endl;
             bool expected = true;
             if (overall_success.compare_exchange_strong(expected, false)) {
                 last_error_message = "Timeout waiting for OpenAI completion.";
             }
        } else {
             std::cout << "   [" << session_id << "] OpenAI processing finished signal received." << std::endl;
        }


        // 6. Finish the TTS Stream
        if (tts_stream_started.load()) {
             std::cout << "   [" << session_id << "] Finishing TTS engine stream..." << std::endl;
             Status tts_status = tts_client_->FinishStream();
             tts_stream_started.store(false); // Mark as finished

             if (!tts_status.ok()) {
                 std::cerr << "❌ [" << session_id << "] TTS stream finish error: (" << tts_status.error_code() << ") "
                           << tts_status.error_message() << std::endl;
                 bool expected = true;
                 if (overall_success.compare_exchange_strong(expected, false)) {
                     last_error_message = "Failed to finish TTS stream: " + tts_status.error_message();
                 }
                 // If TTS finish failed, return its status unless a more critical error happened before
                 if (overall_success.load() == false && last_error_message.find("TTS stream finish error") != std::string::npos) {
                      // Allow returning the specific TTS finish error if it was the *last* error
                 } else if (!overall_success.load()) {
                      // Return the earlier, more critical error message
                       return Status(StatusCode::INTERNAL, "An internal error occurred: " + last_error_message);
                 }
                 // Otherwise return the TTS status directly
                  return tts_status;
             } else {
                 std::cout << "   [" << session_id << "] TTS stream finished successfully." << std::endl;
             }
        } else {
             std::cout << "   [" << session_id << "] TTS stream was not started or already finished. Skipping finish." << std::endl;
        }

        // 7. Final Status Check and Return
        std::cout << "🏁 [" << session_id << "] LLM Processing complete. Final status check." << std::endl;
        if (!overall_success.load()) {
            std::cerr << "❌ [" << session_id << "] Returning INTERNAL error status: " << last_error_message << std::endl;
            return Status(StatusCode::INTERNAL, "An internal error occurred: " + last_error_message);
        } else {
            std::cout << "✅ [" << session_id << "] Returning OK status." << std::endl;
            return Status::OK;
        }

    } catch (const std::exception& e) {
        std::cerr << "❌ [" << session_id << "] Unhandled exception in ProcessTextStream: " << e.what() << std::endl;
        overall_success.store(false);
        last_error_message = "Unhandled exception: " + std::string(e.what());
        cleanup_resources(tts_stream_started.load());
        return Status(StatusCode::UNKNOWN, "An unknown exception occurred in the LLM service handler.");
    } catch (...) {
        std::cerr << "❌ [" << session_id << "] Unknown non-standard exception in ProcessTextStream." << std::endl;
        overall_success.store(false);
        last_error_message = "Unknown non-standard exception.";
        cleanup_resources(tts_stream_started.load());
        return Status(StatusCode::UNKNOWN, "An unknown non-standard exception occurred.");
    }
} // End ProcessTextStream

} // namespace llm_engine