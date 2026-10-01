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

#include "LlmBotsConfig.h"

#include "Config.h"

namespace LlmBots
{

LlmBotsConfig LoadLlmBotsConfig()
{
    LlmBotsConfig cfg;
    cfg.enable = sConfigMgr->GetOption<bool>("LlmBots.Enable", false);
    cfg.provider = sConfigMgr->GetOption<std::string>("LlmBots.Provider", "openai");

    cfg.openAiEndpoint = sConfigMgr->GetOption<std::string>("LlmBots.Openai.Endpoint", "https://api.openai.com");
    cfg.openAiApiKey = sConfigMgr->GetOption<std::string>("LlmBots.Openai.ApiKey", "");
    cfg.modelFast = sConfigMgr->GetOption<std::string>("LlmBots.Openai.ModelFast", "");
    cfg.modelThink = sConfigMgr->GetOption<std::string>("LlmBots.Openai.ModelThink", "");
    cfg.openAiTimeoutMs = sConfigMgr->GetOption<uint32_t>("LlmBots.Openai.TimeoutMs", 30000);

    cfg.foundryEndpoint = sConfigMgr->GetOption<std::string>("LlmBots.Foundry.Endpoint", "");
    cfg.foundryApiKey = sConfigMgr->GetOption<std::string>("LlmBots.Foundry.ApiKey", "");
    cfg.deploymentFast = sConfigMgr->GetOption<std::string>("LlmBots.Foundry.DeploymentFast", "");
    cfg.deploymentThink = sConfigMgr->GetOption<std::string>("LlmBots.Foundry.DeploymentThink", "");
    cfg.foundryTimeoutMs = sConfigMgr->GetOption<uint32_t>("LlmBots.Foundry.TimeoutMs", 30000);

    cfg.mockFailChance = sConfigMgr->GetOption<uint32_t>("LlmBots.Mock.FailChance", 0);

    cfg.workerThreads = sConfigMgr->GetOption<uint32_t>("LlmBots.WorkerThreads", 3);
    cfg.maxCallsPerMinute = sConfigMgr->GetOption<uint32_t>("LlmBots.MaxCallsPerMinute", 20);
    cfg.maxTokensPerHour = sConfigMgr->GetOption<uint32_t>("LlmBots.MaxTokensPerHour", 600000);
    cfg.maxRosterSize = sConfigMgr->GetOption<uint32_t>("LlmBots.MaxRosterSize", 20);
    cfg.botAccount = sConfigMgr->GetOption<std::string>("LlmBots.BotAccount", "llmbots");

    cfg.commanderName = sConfigMgr->GetOption<std::string>("LlmBots.Commander.Name", "CM");
    cfg.commanderAccountId = sConfigMgr->GetOption<uint32_t>("LlmBots.Commander.AccountId", 0);

    cfg.socialCooldownSec = sConfigMgr->GetOption<uint32_t>("LlmBots.Social.CooldownSec", 20);
    cfg.planIntervalSec = sConfigMgr->GetOption<uint32_t>("LlmBots.Plan.IntervalSec", 300);
    cfg.conversationMaxTurns = sConfigMgr->GetOption<uint32_t>("LlmBots.Conversation.MaxTurns", 4);

    cfg.chroniclerName = sConfigMgr->GetOption<std::string>("LlmBots.Chronicler.Name", "");
    cfg.chroniclerDigestSec = sConfigMgr->GetOption<uint32_t>("LlmBots.Chronicler.DigestSec", 60);

    return cfg;
}

} // namespace LlmBots
