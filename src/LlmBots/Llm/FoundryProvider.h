/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 *
 * Microsoft Foundry / Azure OpenAI provider over the OpenAI-compatible
 * /openai/v1/chat/completions REST surface. There is no C/C++ Foundry SDK,
 * so this speaks REST directly:
 *
 *   POST https://<resource>.openai.azure.com/openai/v1/chat/completions
 *        (or https://<resource>.services.ai.azure.com/openai/v1/...)
 *   Headers: api-key: <key>
 *   Body: OpenAI-compatible, deployment name in "model",
 *         response_format with strict json_schema for structured output.
 */

#ifndef FOUNDRY_PROVIDER_H
#define FOUNDRY_PROVIDER_H

#include "LlmProvider.h"

#include <string>

namespace LlmBots
{

class FoundryProvider : public LlmProvider
{
public:
    std::string Name() const override;
    bool Configure(ProviderConfig const& config, std::string& err) override;
    LlmResponse Complete(LlmRequest const& request) override;

private:
    std::string m_endpoint;      // https://host[:port]
    std::string m_host;
    std::string m_port;
    std::string m_pathProbe;     // unused probe result from SplitUrl
    std::string m_apiKey;
    std::string m_deploymentFast;
    std::string m_deploymentThink;
    uint32_t    m_timeoutMs = 30000;

    // Some deployments reject max_tokens (reasoning models want
    // max_completion_tokens). Cache which form the deployment accepts.
    bool m_tokenParamKnown = false;
};

} // namespace LlmBots

#endif // FOUNDRY_PROVIDER_H
