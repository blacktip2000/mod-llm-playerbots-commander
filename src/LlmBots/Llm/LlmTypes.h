/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 *
 * Provider-agnostic LLM core. This directory must not depend on any WoW or
 * playerbots types.
 */

#ifndef LLM_TYPES_H
#define LLM_TYPES_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace LlmBots
{

// Model tier: cheap/fast model for high-frequency calls, larger model for
// rare deliberative calls.
enum class ModelTier : uint8_t
{
    Fast,
    Think
};

// What the call is for. Used for budget accounting and stats.
enum class Purpose : uint8_t
{
    Social = 0,
    Plan,
    Reflection,
    Chronicler,
    Test
};

// Two-tier error classification: transient errors may be retried, permanent
// ones are not.
enum class ErrorKind : uint8_t
{
    None,
    Transient, // 429/5xx/network
    Permanent  // 400/401/404/malformed config
};

// Key/value configuration handed to a provider at Configure() time. The
// module layer fills it from sConfigMgr so the Llm/ core stays free of
// AzerothCore dependencies.
struct ProviderConfig
{
    std::map<std::string, std::string> values;
};

// A tool the model may call. `parameters` is a JSON Schema object, as a
// string, describing the arguments. The Llm core never interprets the tool -
// it only ships the declaration and carries the call and its result back and
// forth.
struct ToolSpec
{
    std::string name;
    std::string description;
    std::string parameters; // JSON Schema for the arguments object
};

// One call the model asked for.
struct ToolCall
{
    std::string id;        // provider's call id - must be echoed back
    std::string name;
    std::string arguments; // JSON object, as a string
};

// The answer to one ToolCall.
struct ToolResult
{
    std::string id;     // matching ToolCall::id
    std::string output; // JSON, as a string
};

struct LlmRequest
{
    ModelTier   tier = ModelTier::Fast;
    Purpose     purpose = Purpose::Social;
    std::string system;      // persona + standing context
    std::string user;        // the event / question
    std::string schemaName;  // key into the LlmSchema registry
    uint32_t    maxOutputTokens = 128;
    float       temperature = 0.9f;

    // Opaque caller token (e.g. the character's guid counter). The Llm core
    // never interprets it; the module uses it to route the completion back.
    uint64_t    requesterId = 0;

    // Tools the model may call for this request. Empty disables tool calling.
    std::vector<ToolSpec> tools;

    // Tool-calling continuation state. A request that comes back with
    // toolCalls is answered by filling toolResults and resubmitting; the
    // provider replays `transcript` so the model sees its own earlier calls.
    // Callers never build these by hand - LlmClient::Resume does it.
    std::string             transcript;   // JSON array of prior input items
    std::vector<ToolResult> toolResults;  // answers to the last round
    uint32_t                toolRound = 0;
};

struct LlmResponse
{
    bool        ok = false;
    ErrorKind   errorKind = ErrorKind::None;
    std::string error;              // human-readable failure reason
    std::string content;            // raw model output, schema-validated
    uint32_t    promptTokens = 0;
    uint32_t    completionTokens = 0;
    uint32_t    latencyMs = 0;

    // Set when the model asked for tools instead of answering. `content` is
    // empty in that case and the caller must run the tools and resume. The
    // transcript is opaque provider state to hand back to Resume().
    bool                  needsTools = false;
    std::vector<ToolCall> toolCalls;
    std::string           transcript;
};

} // namespace LlmBots

#endif // LLM_TYPES_H
