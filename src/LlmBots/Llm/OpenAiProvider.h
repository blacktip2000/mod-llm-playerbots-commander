/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 *
 * OpenAI provider over the Responses API (the recommended, current surface;
 * Chat Completions remains supported but is no longer the migration target):
 *
 *   POST https://api.openai.com/v1/responses
 *   Headers: Authorization: Bearer <key>
 *   Body: model, instructions (system), input (user), text.format with strict
 *         json_schema for structured output, max_output_tokens, temperature.
 *
 * Structured output lives in text.format rather than response_format, and the
 * reply comes back as a typed output[] item array (message -> content ->
 * output_text parts) with usage at the top level.
 */

#ifndef OPENAI_PROVIDER_H
#define OPENAI_PROVIDER_H

#include "LlmProvider.h"

#include <string>

namespace LlmBots
{

class OpenAiProvider : public LlmProvider
{
public:
    std::string Name() const override;
    bool Configure(ProviderConfig const& config, std::string& err) override;
    LlmResponse Complete(LlmRequest const& request) override;

private:
    // Extracts the human-readable message out of the standard
    // {"error":{"message":...}} error envelope.
    static std::string ErrorMessage(std::string const& body);

    std::string m_endpoint;      // https://api.openai.com, overridable
    std::string m_host;
    std::string m_port;
    std::string m_pathProbe;     // unused probe result from SplitUrl
    std::string m_apiKey;
    std::string m_modelFast;
    std::string m_modelThink;
    uint32_t    m_timeoutMs = 30000;
};

} // namespace LlmBots

#endif // OPENAI_PROVIDER_H
