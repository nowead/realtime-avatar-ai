#include "openai_client.h"
#include <iostream>
#include <string>
#include <sstream>
#include <stdexcept>
#include <future> // For async handling example

#include <cpr/cpr.h> // Requires libcpr library
#include <nlohmann/json.hpp>

namespace llm_engine {

using json = nlohmann::json;

OpenAIClient::OpenAIClient(const std::string& key, const std::string& model)
    : api_key_(key), model_name_(model) {
    if (api_key_.empty()) {
        throw std::runtime_error("OpenAI API key cannot be empty.");
    }
    // Could add more validation for model name etc.
    std::cout << "OpenAIClient initialized for model: " << model_name_ << std::endl;
}

// Helper to build the JSON payload for the OpenAI API
json OpenAIClient::build_request_json(const std::vector<ChatMessage>& messages) {
    json payload = {
        {"model", model_name_},
        {"stream", true} // Enable streaming
    };
    json msgs = json::array();
    for (const auto& msg : messages) {
        msgs.push_back({{"role", msg.role}, {"content", msg.content}});
    }
    payload["messages"] = msgs;
    // Add other parameters like temperature, max_tokens etc. if needed
    // payload["temperature"] = 0.7;
    return payload;
}


// Helper to extract content from an SSE data line. Very basic parsing.
std::string OpenAIClient::extract_content_from_sse(const std::string& sse_line) {
    if (sse_line.rfind("data: ", 0) == 0) { // Check if line starts with "data: "
        std::string data_json_str = sse_line.substr(6); // Get string after "data: "

        if (data_json_str == "[DONE]") {
            return ""; // Signal stream end
        }

        try {
            json data = json::parse(data_json_str);
            if (data.contains("choices") && data["choices"].is_array() && !data["choices"].empty()) {
                const auto& choice = data["choices"][0];
                if (choice.contains("delta") && choice["delta"].contains("content")) {
                    // Check if content is null before accessing as string
                    if (!choice["delta"]["content"].is_null()) {
                       return choice["delta"]["content"].get<std::string>();
                    }
                }
            }
        } catch (const json::parse_error& e) {
            std::cerr << "⚠️ OpenAI SSE JSON Parse Error: " << e.what() << " on line: " << data_json_str << std::endl;
        } catch (const std::exception& e) {
             std::cerr << "⚠️ OpenAI SSE Data Error: " << e.what() << " on line: " << data_json_str << std::endl;
        }
    }
    return ""; // Return empty if not valid data or no content
}


// Simplified asynchronous streaming implementation using std::async
// A more robust implementation might use a dedicated thread or io_context.
void OpenAIClient::StreamChatCompletion(
    const std::vector<ChatMessage>& messages,
    const OpenAIChunkCallback& chunk_callback,
    const OpenAICompletionCallback& completion_callback)
{
    if (!chunk_callback || !completion_callback) {
         throw std::runtime_error("OpenAI callbacks cannot be null.");
    }

    json payload = build_request_json(messages);
    std::string payload_str = payload.dump();

    // Launch the request asynchronously
    std::async(std::launch::async, [this, payload_str, chunk_callback, completion_callback]() {
        std::string accumulated_sse_data;
        bool stream_successful = true;
        std::string error_msg;

        try {
             // Define a CPR WriteCallback to process chunks as they arrive
             auto write_callback = [&](std::string data, intptr_t /*userdata*/) -> bool {
                accumulated_sse_data += data;
                size_t pos;
                // Process line by line (\n\n is the SSE message separator)
                while ((pos = accumulated_sse_data.find("\n\n")) != std::string::npos) {
                    std::string message = accumulated_sse_data.substr(0, pos);
                    accumulated_sse_data.erase(0, pos + 2); // Erase message + "\n\n"

                    // Process each line within the message (\n separator for lines like event:, data:, id:)
                    std::stringstream ss(message);
                    std::string line;
                    while (std::getline(ss, line, '\n')) {
                        if (!line.empty() && line.back() == '\r') { // Handle potential \r\n
                            line.pop_back();
                        }
                         if (line.empty()) continue; // Skip empty lines

                        std::string content = extract_content_from_sse(line);
                        if (!content.empty()) {
                            try {
                                 chunk_callback(content); // Pass content chunk to user callback
                            } catch (const std::exception& cb_ex) {
                                 std::cerr << "❌ Exception in OpenAI chunk_callback: " << cb_ex.what() << std::endl;
                                 // Decide if this should abort the stream
                            }
                        } else if (line.rfind("data: ", 0) == 0 && line.substr(6) == "[DONE]") {
                             // Stream is done, but we wait for the request to fully finish below.
                             std::cout << "   OpenAI stream indicated [DONE]." << std::endl;
                        }
                         // Handle other SSE fields like 'event:' or 'id:' if necessary
                    }
                }
                return true; // Return true to continue receiving data
            };

            std::cout << "   Sending request to OpenAI..." << std::endl;
            auto response = cpr::Post(
                cpr::Url{api_endpoint_},
                cpr::Header{{"Authorization", "Bearer " + api_key_},
                            {"Content-Type", "application/json"},
                            {"Accept", "text/event-stream"}}, // Expect SSE
                cpr::Body{payload_str},
                cpr::WriteCallback{write_callback} // Process stream here
                // cpr::Timeout{30000} // Optional timeout
            );

             std::cout << "   OpenAI request finished. Status: " << response.status_code << std::endl;

            // Check final response status
            if (response.status_code < 200 || response.status_code >= 300) {
                stream_successful = false;
                error_msg = "OpenAI API Error: HTTP " + std::to_string(response.status_code) + " - " + response.error.message + " Body: " + response.text;
                std::cerr << "❌ " << error_msg << std::endl;
            } else {
                 // Process any remaining data in the buffer after request ends
                 // (Sometimes [DONE] might be the last thing without trailing \n\n)
                 if (!accumulated_sse_data.empty()) {
                      std::stringstream ss(accumulated_sse_data);
                      std::string line;
                       while (std::getline(ss, line, '\n')) {
                           if (!line.empty() && line.back() == '\r') line.pop_back();
                           if (line.empty()) continue;
                           std::string content = extract_content_from_sse(line);
                           if (!content.empty()) chunk_callback(content);
                       }
                 }
            }

        } catch (const std::exception& e) {
            stream_successful = false;
            error_msg = "Exception during OpenAI request: " + std::string(e.what());
            std::cerr << "❌ " << error_msg << std::endl;
        }

        // Call the final completion callback
        try {
             completion_callback(stream_successful, error_msg);
        } catch (const std::exception& cb_ex) {
             std::cerr << "❌ Exception in OpenAI completion_callback: " << cb_ex.what() << std::endl;
        }
    }); // End of std::async lambda
}

} // namespace llm_engine