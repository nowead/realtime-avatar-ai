#include "llm_service.h"
#include <iostream>
#include <string>
#include <vector>
#include <random> 
#include <sstream> 
#include <iomanip> 
#include <thread>  
#include <chrono>
#include <future>  

using grpc::StatusCode;

namespace llm_engine {

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

void LLMServiceImpl::handle_openai_chunk(const std::string& session_id, const std::string& chunk, std::atomic<bool>& tts_stream_ok) {
    if (!tts_stream_ok.load()) {
         return; 
    }
    if (!tts_client_->SendTextChunk(chunk)) {
        std::cerr << "❌ [" << session_id << "] Failed to send chunk to TTS service. Marking TTS stream as failed." << std::endl;
        tts_stream_ok.store(false); 
    }
}

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
        bool expected = true;
        if(overall_success.compare_exchange_strong(expected, false)) {
             last_error = "OpenAI streaming failed: " + error_message;
        }
     }
     try {
         openai_done_promise.set_value();
     } catch (const std::future_error& e) {
          std::cerr << "ℹ️ [" << session_id << "] OpenAI promise already set in completion handler: " << e.what() << std::endl;
     }
}

Status LLMServiceImpl::ProcessTextStream(
    ServerContext* context,
    ServerReader<LLMStreamRequest>* reader,
    Empty* response)
{
    const std::string client_peer = context->peer();
    std::string llm_internal_session_id;
    std::string frontend_session_id;

    std::vector<ChatMessage> chat_history; 

    std::atomic<bool> tts_stream_started{false};
    std::atomic<bool> tts_stream_ok{true}; 
    std::atomic<bool> openai_processing_started{false};
    std::atomic<bool> overall_success{true}; 
    std::string last_error_message;

    std::promise<void> openai_done_promise;
    auto openai_done_future = openai_done_promise.get_future();

    auto cleanup_resources = [&](bool finish_tts) {
        std::cout << "🧹 LLM_Service [LLM_SID:" << (llm_internal_session_id.empty() ? "NO_LLM_SID" : llm_internal_session_id) 
                  << ", FE_SID:" << (frontend_session_id.empty() ? "NO_FE_SID" : frontend_session_id)
                  << "] Cleaning up LLM resources... FinishTTS=" << finish_tts << std::endl;
        if (finish_tts && tts_client_ && tts_stream_started.load() && tts_client_->IsStreamActive()) {
            std::cout << "   Finishing TTS stream for FE_SID [" << frontend_session_id << "]..." << std::endl;
            Status tts_status = tts_client_->FinishStream();
            if (!tts_status.ok()) {
                std::cerr << "   ⚠️ TTS stream finish error during cleanup: ("
                          << tts_status.error_code() << ") " << tts_status.error_message() << std::endl;
                 bool expected = true;
                 if (overall_success.compare_exchange_strong(expected, false)) {
                      last_error_message = "TTS stream finish error: " + tts_status.error_message();
                 }
            } else {
                std::cout << "   TTS stream finished successfully during cleanup for FE_SID [" << frontend_session_id << "]." << std::endl;
            }
            tts_stream_started.store(false); 
        }
         try { openai_done_promise.set_value(); } catch (const std::future_error&) {}
    };


    try {
        LLMStreamRequest initial_request;
        std::cout << "   LLM_Service: Waiting for initial SessionConfig from client " << client_peer << "..." << std::endl;
        if (!reader->Read(&initial_request)) {
            last_error_message = "Failed to read initial request from client.";
            std::cerr << "❌ LLM_Service [Peer:" << client_peer << "] " << last_error_message << std::endl;
            overall_success.store(false);
            return Status(StatusCode::INVALID_ARGUMENT, last_error_message);
        }

        if (initial_request.request_data_case() != LLMStreamRequest::kConfig) {
            last_error_message = "Initial request must be SessionConfig.";
            std::cerr << "❌ LLM_Service [Peer:" << client_peer << "] " << last_error_message << std::endl;
            overall_success.store(false);
            return Status(StatusCode::INVALID_ARGUMENT, last_error_message);
        }

        const auto& received_config = initial_request.config();
        frontend_session_id = received_config.frontend_session_id();
        llm_internal_session_id = received_config.session_id();
        if (llm_internal_session_id.empty()) {
            llm_internal_session_id = generate_uuid();
        }
        if (frontend_session_id.empty()) {
             last_error_message = "CRITICAL: frontend_session_id is missing in SessionConfig from STT.";
             std::cerr << "❌ LLM_Service [LLM_SID:" << llm_internal_session_id << "] " << last_error_message << std::endl;
             overall_success.store(false);
             return Status(StatusCode::INVALID_ARGUMENT, last_error_message);
        }
        std::cout << "   LLM_Service [LLM_SID:" << llm_internal_session_id 
                  << ", FE_SID:" << frontend_session_id 
                  << "] Config received. STT internal Session ID = " << received_config.session_id() << std::endl;
        
        std::string tts_language = "ko-KR"; 
        std::string tts_voice = "ko-KR-SunHiNeural";
        std::string system_prompt = "You are a helpful assistant."; 

        chat_history.push_back({"system", system_prompt});

        std::cout << "   LLM_Service [LLM_SID:" << llm_internal_session_id << "] Starting stream to TTS Service for FE_SID [" << frontend_session_id << "]..." << std::endl;
        tts::SynthesisConfig tts_config_to_send; 
        tts_config_to_send.set_frontend_session_id(frontend_session_id);       
        tts_config_to_send.set_session_id(llm_internal_session_id);      
        tts_config_to_send.set_language_code(tts_language);
        tts_config_to_send.set_voice_name(tts_voice);

        if (!tts_client_->StartStream(tts_config_to_send)) {
            last_error_message = "Failed to start stream to TTS Service.";
            std::cerr << "❌ LLM_Service [LLM_SID:" << llm_internal_session_id << "] " << last_error_message << std::endl;
            overall_success.store(false);
            return Status(StatusCode::INTERNAL, last_error_message);
        }
        tts_stream_started.store(true);
        tts_stream_ok.store(true); 
        std::cout << "   LLM_Service [LLM_SID:" << llm_internal_session_id << "] TTS stream started successfully for FE_SID [" << frontend_session_id << "]." << std::endl;

         std::string accumulated_text;
         LLMStreamRequest chunk_request;
         std::cout << "   LLM_Service [LLM_SID:" << llm_internal_session_id << "] Waiting for text chunks from client..." << std::endl;

        while (overall_success.load() && !context->IsCancelled() && reader->Read(&chunk_request)) {
            if (chunk_request.request_data_case() == LLMStreamRequest::kTextChunk) {
                const std::string& chunk = chunk_request.text_chunk();
                 if (!chunk.empty()) {
                     accumulated_text += chunk;
                 }
            } else if (chunk_request.request_data_case() == LLMStreamRequest::kConfig) {
                 std::cerr << "⚠️ LLM_Service [LLM_SID:" << llm_internal_session_id << "] Received unexpected Config message after initialization. Ignoring." << std::endl;
            } else {
                 std::cerr << "⚠️ LLM_Service [LLM_SID:" << llm_internal_session_id << "] Received message with unexpected type. Ignoring." << std::endl;
            }
        } 

        if (context->IsCancelled()) {
             std::cout << "ℹ️ LLM_Service [LLM_SID:" << llm_internal_session_id << "] Client cancelled the request." << std::endl;
             last_error_message = "Request cancelled by client.";
             overall_success.store(false);
             cleanup_resources(true); 
             return Status(StatusCode::CANCELLED, last_error_message);
        }
        if (!overall_success.load()) {
             std::cerr << "❌ LLM_Service [LLM_SID:" << llm_internal_session_id << "] Error occurred. Exiting read loop. Reason: " << last_error_message << std::endl;
             cleanup_resources(tts_stream_started.load()); 
             return Status(StatusCode::INTERNAL, "Internal error during stream processing: " + last_error_message);
        }

        std::cout << "ℹ️ LLM_Service [LLM_SID:" << llm_internal_session_id << "] Client finished sending text chunks." << std::endl;
        std::cout << "   LLM_Service [LLM_SID:" << llm_internal_session_id << "] Accumulated Text: '" << accumulated_text << "'" << std::endl;

         if (!accumulated_text.empty()) {
             chat_history.push_back({"user", accumulated_text});
         } else {
              std::cout << "   LLM_Service [LLM_SID:" << llm_internal_session_id << "] No text accumulated from client. Skipping OpenAI call." << std::endl;
              cleanup_resources(true); 
              return Status::OK; 
         }

         std::cout << "   LLM_Service [LLM_SID:" << llm_internal_session_id << "] Starting OpenAI streaming processing..." << std::endl;
         openai_processing_started.store(true);

        auto openai_chunk_cb = [this, llm_sid = llm_internal_session_id, &tts_stream_ok](const std::string& chunk) {
            this->handle_openai_chunk(llm_sid, chunk, tts_stream_ok);
        };
        auto openai_completion_cb = 
            [this, llm_sid = llm_internal_session_id, &openai_done_promise, &overall_success, &last_error_message]
            (bool success, const std::string& error_msg) {
            this->handle_openai_completion(llm_sid, success, error_msg, openai_done_promise, overall_success, last_error_message);
        };

        try {
            openai_client_->StreamChatCompletion(chat_history, openai_chunk_cb, openai_completion_cb);
        } catch (const std::exception& e) {
            last_error_message = "Failed to start OpenAI streaming: " + std::string(e.what());
            std::cerr << "❌ LLM_Service [LLM_SID:" << llm_internal_session_id << "] " << last_error_message << std::endl;
            overall_success.store(false);
            openai_processing_started.store(false);
             try { openai_done_promise.set_value(); } catch(...) {} 
             cleanup_resources(tts_stream_started.load());
             return Status(StatusCode::INTERNAL, last_error_message);
        }

        std::cout << "   LLM_Service [LLM_SID:" << llm_internal_session_id << "] Waiting for OpenAI processing to complete..." << std::endl;
        std::future_status wait_status = openai_done_future.wait_for(std::chrono::seconds(60)); 

        if (wait_status == std::future_status::timeout) {
             std::cerr << "❌ LLM_Service [LLM_SID:" << llm_internal_session_id << "] Timed out waiting for OpenAI completion (60s)." << std::endl;
             bool expected = true;
             if (overall_success.compare_exchange_strong(expected, false)) {
                 last_error_message = "Timeout waiting for OpenAI completion.";
             }
        } else {
             std::cout << "   LLM_Service [LLM_SID:" << llm_internal_session_id << "] OpenAI processing finished signal received." << std::endl;
        }

        if (tts_stream_started.load()) {
             std::cout << "   LLM_Service [LLM_SID:" << llm_internal_session_id << "] Finishing TTS engine stream for FE_SID [" << frontend_session_id << "]..." << std::endl;
             Status tts_status = tts_client_->FinishStream();
             tts_stream_started.store(false); 

             if (!tts_status.ok()) {
                 std::cerr << "❌ LLM_Service [LLM_SID:" << llm_internal_session_id << "] TTS stream finish error: (" << tts_status.error_code() << ") "
                           << tts_status.error_message() << std::endl;
                 bool expected = true;
                 if (overall_success.compare_exchange_strong(expected, false)) {
                     last_error_message = "Failed to finish TTS stream: " + tts_status.error_message();
                 }
                 if (overall_success.load() == false && last_error_message.find("TTS stream finish error") != std::string::npos) {
                 } else if (!overall_success.load()) {
                       return Status(StatusCode::INTERNAL, "An internal error occurred: " + last_error_message);
                 }
                  return tts_status;
             } else {
                 std::cout << "   LLM_Service [LLM_SID:" << llm_internal_session_id << "] TTS stream finished successfully for FE_SID [" << frontend_session_id << "]." << std::endl;
             }
        } else {
             std::cout << "   LLM_Service [LLM_SID:" << llm_internal_session_id << "] TTS stream was not started or already finished. Skipping finish." << std::endl;
        }

        std::cout << "🏁 LLM_Service [LLM_SID:" << llm_internal_session_id << "] LLM Processing complete. Final status check." << std::endl;
        if (!overall_success.load()) {
            std::cerr << "❌ LLM_Service [LLM_SID:" << llm_internal_session_id << "] Returning INTERNAL error status: " << last_error_message << std::endl;
            return Status(StatusCode::INTERNAL, "An internal error occurred: " + last_error_message);
        } else {
            std::cout << "✅ LLM_Service [LLM_SID:" << llm_internal_session_id << "] Returning OK status." << std::endl;
            return Status::OK;
        }

    } catch (const std::exception& e) {
        std::cerr << "❌ LLM_Service [LLM_SID:" << (llm_internal_session_id.empty() ? "N/A" : llm_internal_session_id) 
                  << ", FE_SID:" << (frontend_session_id.empty() ? "N/A" : frontend_session_id)
                  << "] Unhandled exception in ProcessTextStream: " << e.what() << std::endl;
        overall_success.store(false);
        last_error_message = "Unhandled exception: " + std::string(e.what());
        cleanup_resources(tts_stream_started.load());
        return Status(StatusCode::UNKNOWN, "An unknown exception occurred in the LLM service handler.");
    } catch (...) {
        std::cerr << "❌ LLM_Service [LLM_SID:" << (llm_internal_session_id.empty() ? "N/A" : llm_internal_session_id) 
                  << ", FE_SID:" << (frontend_session_id.empty() ? "N/A" : frontend_session_id)
                  << "] Unknown non-standard exception in ProcessTextStream." << std::endl;
        overall_success.store(false);
        last_error_message = "Unknown non-standard exception.";
        cleanup_resources(tts_stream_started.load());
        return Status(StatusCode::UNKNOWN, "An unknown non-standard exception occurred.");
    }
} 

} // namespace llm_engine
