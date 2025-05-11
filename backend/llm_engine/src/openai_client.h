#pragma once

#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <nlohmann/json.hpp>

namespace llm_engine {

using OpenAIChunkCallback = std::function<void(const std::string& chunk)>;
using OpenAICompletionCallback = std::function<void(bool success, const std::string& error_message)>;

struct ChatMessage {
    std::string role;
    std::string content;
};

class OpenAIClient {
public:
    explicit OpenAIClient(const std::string& api_key, const std::string& model = "gpt-4o");
    ~OpenAIClient() = default;

    // Deleted copy/move constructors and assignment operators
    OpenAIClient(const OpenAIClient&) = delete;
    OpenAIClient& operator=(const OpenAIClient&) = delete;
    OpenAIClient(OpenAIClient&&) = delete;
    OpenAIClient& operator=(OpenAIClient&&) = delete;

    // Starts a streaming chat completion request
    // Takes the conversation history and callbacks
    // This function would likely run asynchronously or manage an async task
    void StreamChatCompletion(
        const std::vector<ChatMessage>& messages,
        const OpenAIChunkCallback& chunk_callback,
        const OpenAICompletionCallback& completion_callback);

    // Potentially add a method to signal the end of input if needed,
    // though typically chat completion takes the full history at once.

private:
    std::string api_key_;
    std::string model_name_;
    std::string api_endpoint_ = "https://api.openai.com/v1/chat/completions"; // Example endpoint

    // Helper to parse SSE data line
    std::string extract_content_from_sse(const std::string& sse_line);
    nlohmann::json build_request_json(const std::vector<ChatMessage>& messages);

    // Internal state for managing async requests if needed
    // std::atomic<bool> streaming_active_{false};
    // std::thread worker_thread_; // Example for async handling
};

} // namespace llm_engine