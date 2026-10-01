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
 *
 * Module configuration, read from sConfigMgr once at startup (config file and
 * AC_* environment variables are resolved there; env wins over file).
 *
 * Roster and persona changes are the hot path and do NOT go through config:
 * they are re-read from the database by .llmbot reload without a restart.
 */

#ifndef LLM_BOTS_CONFIG_H
#define LLM_BOTS_CONFIG_H

#include <cstdint>
#include <string>

namespace LlmBots
{

struct LlmBotsConfig
{
    bool enable = false;
    std::string provider = "openai";

    // OpenAI provider (default).
    std::string openAiEndpoint; // default https://api.openai.com
    std::string openAiApiKey;
    std::string modelFast;
    std::string modelThink;
    uint32_t    openAiTimeoutMs = 30000;

    // Foundry provider.
    std::string foundryEndpoint;
    std::string foundryApiKey;
    std::string deploymentFast;
    std::string deploymentThink;
    uint32_t    foundryTimeoutMs = 30000;

    // Mock provider.
    uint32_t mockFailChance = 0;

    // Budget / workers.
    uint32_t workerThreads = 3;
    uint32_t maxCallsPerMinute = 20;
    uint32_t maxTokensPerHour = 600000;
    uint32_t maxRosterSize = 20;
    std::string botAccount = "llmbots";

    // Commander.
    std::string commanderName = "CM";
    uint32_t commanderAccountId = 0;

    // Cadence.
    uint32_t socialCooldownSec = 20;
    uint32_t planIntervalSec = 300;
    uint32_t conversationMaxTurns = 4;

    // Chronicler.
    std::string chroniclerName;
    uint32_t chroniclerDigestSec = 60;
};

// Reads every option from sConfigMgr. Call once at module startup.
LlmBotsConfig LoadLlmBotsConfig();

} // namespace LlmBots

#endif // LLM_BOTS_CONFIG_H
