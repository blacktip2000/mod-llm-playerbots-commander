/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 */

#include "FoundryProvider.h"

#include "HttpsClient.h"
#include "LlmSchema.h"

#include "json.hpp"

#include <atomic>
#include <chrono>

using nlohmann::json;

namespace LlmBots
{

namespace
{

char const* kChatCompletionsPath = "/openai/v1/chat/completions";

std::string GetConfig(ProviderConfig const& config, std::string const& key, std::string const& fallback = {})
{
    auto const itr = config.values.find(key);
    return itr == config.values.end() ? fallback : itr->second;
}

std::string Trim(std::string s)
{
    while (!s.empty() && (s.back() == '/' || s.back() == ' '))
    {
        s.pop_back();
    }
    return s;
}

} // namespace

std::string FoundryProvider::Name() const
{
    return "foundry";
}

bool FoundryProvider::Configure(ProviderConfig const& config, std::string& err)
{
    m_endpoint = Trim(GetConfig(config, "Endpoint"));
    m_apiKey = GetConfig(config, "ApiKey");
    m_deploymentFast = GetConfig(config, "DeploymentFast");
    m_deploymentThink = GetConfig(config, "DeploymentThink");

    std::string const timeout = GetConfig(config, "TimeoutMs", "30000");
    try
    {
        m_timeoutMs = static_cast<uint32_t>(std::stoul(timeout));
    }
    catch (...)
    {
        m_timeoutMs = 30000;
    }

    if (m_endpoint.empty())
    {
        err = "LlmBots.Foundry.Endpoint is not configured";
        return false;
    }
    if (m_apiKey.empty())
    {
        err = "LlmBots.Foundry.ApiKey is not configured";
        return false;
    }
    if (m_deploymentFast.empty())
    {
        err = "LlmBots.Foundry.DeploymentFast is not configured";
        return false;
    }
    if (m_deploymentThink.empty())
    {
        err = "LlmBots.Foundry.DeploymentThink is not configured";
        return false;
    }

    if (!SplitUrl(m_endpoint, m_host, m_port, m_pathProbe))
    {
        err = "LlmBots.Foundry.Endpoint is not a valid https:// URL";
        return false;
    }

    m_tokenParamKnown = false;
    return true;
}

LlmResponse FoundryProvider::Complete(LlmRequest const& request)
{
    LlmResponse response;
    auto const start = std::chrono::steady_clock::now();

    std::string const deployment = request.tier == ModelTier::Think ? m_deploymentThink : m_deploymentFast;
    std::string const schemaStr = GetSchema(request.schemaName);
    if (schemaStr.empty())
    {
        response.ok = false;
        response.errorKind = ErrorKind::Permanent;
        response.error = "unknown schema '" + request.schemaName + "'";
        return response;
    }

    json body = json::object();
    body["model"] = deployment;
    body["temperature"] = request.temperature;
    body["messages"] = json::array(
        { json::object({ { "role", "system" }, { "content", request.system } }),
          json::object({ { "role", "user" }, { "content", request.user } }) });

    json schema;
    try
    {
        schema = json::parse(schemaStr);
    }
    catch (json::exception const& e)
    {
        response.ok = false;
        response.errorKind = ErrorKind::Permanent;
        response.error = std::string("schema does not parse: ") + e.what();
        return response;
    }

    body["response_format"] = json::object({
        { "type", "json_schema" },
        { "json_schema", json::object({
            { "name", request.schemaName },
            { "strict", true },
            { "schema", schema } }) }
    });

    std::string const path = m_pathProbe + kChatCompletionsPath;
    std::vector<std::pair<std::string, std::string>> headers = { { "api-key", m_apiKey } };

    auto doCall = [&](std::string const& tokenParam) -> HttpResponse
    {
        body[tokenParam] = request.maxOutputTokens;
        return HttpsPostJson(m_host, m_port, path, body.dump(), headers, m_timeoutMs);
    };

    HttpResponse res = doCall("max_completion_tokens");

    // Some deployments reject max_completion_tokens and want max_tokens.
    // Detect by the 400 mentioning the parameter, retry once, cache the form.
    if (!res.ok && res.status == 400 && res.body.find("max_completion_tokens") != std::string::npos)
    {
        res = doCall("max_tokens");
        m_tokenParamKnown = true;
    }
    else if (res.ok && !m_tokenParamKnown)
    {
        m_tokenParamKnown = true;
    }

    response.latencyMs = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());

    if (!res.ok)
    {
        response.ok = false;
        response.error = res.error.empty() ? "HTTP failure" : res.error;
        response.errorKind = ErrorKind::Transient;
        return response;
    }

    if (res.status != 200)
    {
        response.ok = false;
        response.error = "HTTP " + std::to_string(res.status) + ": " + res.body.substr(0, 300);
        response.errorKind = (res.status == 429 || res.status >= 500) ? ErrorKind::Transient : ErrorKind::Permanent;
        return response;
    }

    try
    {
        json parsed = json::parse(res.body);
        if (parsed.contains("usage"))
        {
            response.promptTokens = parsed["usage"].value("prompt_tokens", 0u);
            response.completionTokens = parsed["usage"].value("completion_tokens", 0u);
        }
        if (!parsed.contains("choices") || parsed["choices"].empty())
        {
            response.ok = false;
            response.errorKind = ErrorKind::Permanent;
            response.error = "no choices in response";
            return response;
        }
        json const& choice = parsed["choices"][0];
        if (!choice.contains("message") || !choice["message"].contains("content"))
        {
            response.ok = false;
            response.errorKind = ErrorKind::Permanent;
            response.error = "no message content in response";
            return response;
        }
        response.content = choice["message"]["content"].get<std::string>();
    }
    catch (json::exception const& e)
    {
        response.ok = false;
        response.errorKind = ErrorKind::Permanent;
        response.error = std::string("response does not parse: ") + e.what();
        return response;
    }

    std::string const problem = ValidateOutput(request.schemaName, response.content);
    if (!problem.empty())
    {
        response.ok = false;
        response.errorKind = ErrorKind::Permanent;
        response.error = "schema validation failed: " + problem;
        return response;
    }

    response.ok = true;
    return response;
}

} // namespace LlmBots
