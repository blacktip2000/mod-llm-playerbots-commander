/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 */

#include "MockProvider.h"

#include "LlmSchema.h"

#include "json.hpp"

using nlohmann::json;

namespace LlmBots
{

std::string MockProvider::Name() const
{
    return "mock";
}

bool MockProvider::Configure(ProviderConfig const& config, std::string& err)
{
    auto const itr = config.values.find("FailChance");
    m_failChancePct = itr == config.values.end() ? 0u : static_cast<uint32_t>(std::stoul(itr->second));
    if (m_failChancePct > 100)
    {
        m_failChancePct = 100;
    }
    err.clear();
    return true;
}

LlmResponse MockProvider::Complete(LlmRequest const& request)
{
    LlmResponse response;
    ++m_callCount;

    // Deterministic, non-repeating failure cadence so long-running tests
    // exercise the error path regularly without being unpredictable.
    if (m_failChancePct > 0 && m_callCount % (100 / m_failChancePct) == 0)
    {
        response.ok = false;
        response.errorKind = ErrorKind::Transient;
        response.error = "mock transient failure";
        return response;
    }

    json out = json::object();
    if (request.schemaName == "bot_utterance")
    {
        out["line"] = "A curious whisper stirs the air. (mock)";
        out["emote"] = nullptr;
        out["channel"] = "say";
    }
    else if (request.schemaName == "bot_agenda")
    {
        out["goal"] = "grind";
        out["zone"] = nullptr;
        out["social_stance"] = "solo";
        out["strategy_toggles"] = json::array({});
        out["narrative"] = "Just another day of honest work. (mock)";
        out["reason"] = "mock planning response";
    }
    else if (request.schemaName == "chronicler_digest")
    {
        out["headline"] = "The week in taverns. (mock)";
        out["digest"] = "Nothing much happened. The mock chronicler remains vigilant.";
    }
    else
    {
        response.ok = false;
        response.errorKind = ErrorKind::Permanent;
        response.error = "unknown schema '" + request.schemaName + "'";
        return response;
    }

    response.content = out.dump();
    response.promptTokens = 64;
    response.completionTokens = static_cast<uint32_t>(out.dump().size() / 4) + 4;
    response.latencyMs = 2;
    response.ok = true;
    return response;
}

} // namespace LlmBots
