/*
 * Modified distribution: mod-playerbots-llm-commander.
 * This file differs from bigr00/mod-llm-playerbots c0ba652.
 * Modified version published on 2026-10-01.
 * Modification notice added on 2026-10-01.
 * Publication date does not identify every historical edit date.
 * Original copyright and license notices remain in effect.
 */
/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 */

#include "OpenAiProvider.h"

#include "HttpsClient.h"
#include "LlmSchema.h"
#include "Log.h"

#include "json.hpp"

#include <chrono>

using nlohmann::json;

namespace LlmBots
{

namespace
{

char const* kResponsesPath = "/v1/responses";

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

std::string OpenAiProvider::Name() const
{
    return "openai";
}

bool OpenAiProvider::Configure(ProviderConfig const& config, std::string& err)
{
    m_endpoint = Trim(GetConfig(config, "OpenaiEndpoint", "https://api.openai.com"));
    m_apiKey = GetConfig(config, "OpenaiApiKey");
    m_modelFast = GetConfig(config, "OpenaiModelFast");
    m_modelThink = GetConfig(config, "OpenaiModelThink");

    std::string const timeout = GetConfig(config, "OpenaiTimeoutMs", "30000");
    try
    {
        m_timeoutMs = static_cast<uint32_t>(std::stoul(timeout));
    }
    catch (...)
    {
        m_timeoutMs = 30000;
    }

    if (m_apiKey.empty())
    {
        err = "LlmBots.Openai.ApiKey is not configured";
        return false;
    }
    if (m_modelFast.empty())
    {
        err = "LlmBots.Openai.ModelFast is not configured";
        return false;
    }
    if (m_modelThink.empty())
    {
        err = "LlmBots.Openai.ModelThink is not configured";
        return false;
    }

    if (!SplitUrl(m_endpoint, m_host, m_port, m_pathProbe))
    {
        err = "LlmBots.Openai.Endpoint is not a valid https:// URL";
        return false;
    }

    return true;
}

LlmResponse OpenAiProvider::Complete(LlmRequest const& request)
{
    LlmResponse response;
    auto const start = std::chrono::steady_clock::now();

    std::string const model = request.tier == ModelTier::Think ? m_modelThink : m_modelFast;
    std::string const schemaStr = GetSchema(request.schemaName);
    if (schemaStr.empty())
    {
        response.ok = false;
        response.errorKind = ErrorKind::Permanent;
        response.error = "unknown schema '" + request.schemaName + "'";
        return response;
    }

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

    // The input is always an array so a tool-calling conversation can grow in
    // place. `store` is false, so previous_response_id chaining is not
    // available and the whole transcript is replayed on every round.
    json input = json::array();
    if (request.transcript.empty())
    {
        input.push_back(json::object({ { "role", "user" }, { "content", request.user } }));
    }
    else
    {
        try
        {
            input = json::parse(request.transcript);
        }
        catch (json::exception const& e)
        {
            response.ok = false;
            response.errorKind = ErrorKind::Permanent;
            response.error = std::string("tool transcript does not parse: ") + e.what();
            return response;
        }
    }

    // Answers to the previous round. The function_call items they refer to are
    // already in the transcript; only the outputs are appended.
    for (auto const& result : request.toolResults)
    {
        input.push_back(json::object({
            { "type", "function_call_output" },
            { "call_id", result.id },
            { "output", result.output } }));
    }

    json body = json::object();
    body["model"] = model;
    body["instructions"] = request.system;
    body["input"] = input;
    body["max_output_tokens"] = request.maxOutputTokens;

    if (!request.tools.empty())
    {
        json tools = json::array();
        for (auto const& tool : request.tools)
        {
            json parameters;
            try
            {
                parameters = json::parse(tool.parameters);
            }
            catch (json::exception const& ex)
            {
                LOG_ERROR(
                    "llmbots.provider",
                    "Dropping malformed tool '{}' because parameters JSON failed to parse: {}",
                    tool.name,
                    ex.what());
                continue; // a malformed tool declaration is dropped, not fatal
            }
            tools.push_back(json::object({
                { "type", "function" },
                { "name", tool.name },
                { "description", tool.description },
                { "parameters", parameters },
                { "strict", true } }));
        }
        if (!tools.empty())
        {
            body["tools"] = tools;
        }
    }
    // Note: no temperature - reasoning models (gpt-5.x) reject the parameter.
    // Omitting it is valid for every model; the API applies its default.
    body["store"] = false; // do not keep prompts on OpenAI servers
    body["text"] = json::object({
        { "format", json::object({
            { "type", "json_schema" },
            { "name", request.schemaName },
            { "strict", true },
            { "schema", schema } }) }
    });

    std::string const path = m_pathProbe + kResponsesPath;
    std::vector<std::pair<std::string, std::string>> headers = {
        { "Authorization", "Bearer " + m_apiKey },
        { "Content-Type", "application/json" }
    };

    HttpResponse res = HttpsPostJson(m_host, m_port, path, body.dump(), headers, m_timeoutMs);

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
        response.error = "HTTP " + std::to_string(res.status) + ": " + ErrorMessage(res.body);
        response.errorKind = (res.status == 429 || res.status >= 500) ? ErrorKind::Transient : ErrorKind::Permanent;
        return response;
    }

    try
    {
        json parsed = json::parse(res.body);
        if (parsed.contains("usage"))
        {
            response.promptTokens = parsed["usage"].value("input_tokens", 0u);
            response.completionTokens = parsed["usage"].value("output_tokens", 0u);
        }

        // The reply is a typed output[] item list: find the message item and
        // join its output_text content parts.
        std::string content;
        std::string refusal;
        json toolItems = json::array();
        if (parsed.contains("output") && parsed["output"].is_array())
        {
            for (auto const& item : parsed["output"])
            {
                if (!item.contains("type"))
                {
                    continue;
                }
                std::string const type = item["type"].get<std::string>();
                if (type == "function_call")
                {
                    ToolCall call;
                    call.id = item.value("call_id", "");
                    call.name = item.value("name", "");
                    call.arguments = item.value("arguments", "{}");
                    if (!call.id.empty() && !call.name.empty())
                    {
                        response.toolCalls.push_back(std::move(call));
                        toolItems.push_back(item);
                    }
                    continue;
                }
                if (type == "message" && item.contains("content") && item["content"].is_array())
                {
                    for (auto const& part : item["content"])
                    {
                        if (part.value("type", "") == "output_text" && part.contains("text"))
                        {
                            content += part["text"].get<std::string>();
                        }
                    }
                }
                else if (type == "message" && item.contains("refusal"))
                {
                    refusal = item["refusal"].get<std::string>();
                }
            }
        }

        if (!refusal.empty())
        {
            response.ok = false;
            response.errorKind = ErrorKind::Permanent;
            response.error = "model refused: " + refusal;
            return response;
        }

        // The model asked for tools instead of answering. Hand the caller the
        // calls plus the transcript to replay, and let it resume once the
        // results are in. The function_call items must be carried forward or
        // the follow-up request has outputs referring to calls it cannot see.
        if (!response.toolCalls.empty())
        {
            for (auto const& item : toolItems)
            {
                input.push_back(item);
            }
            response.needsTools = true;
            response.transcript = input.dump();
            response.ok = true;
            return response;
        }

        if (content.empty())
        {
            response.ok = false;
            response.errorKind = ErrorKind::Permanent;
            response.error = "no output_text content in response";
            return response;
        }
        response.content = content;
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

std::string OpenAiProvider::ErrorMessage(std::string const& body)
{
    try
    {
        json parsed = json::parse(body);
        if (parsed.contains("error") && parsed["error"].contains("message"))
        {
            return parsed["error"]["message"].get<std::string>();
        }
    }
    catch (json::exception const&)
    {
    }
    return body.substr(0, 300);
}

} // namespace LlmBots
