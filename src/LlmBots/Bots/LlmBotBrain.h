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
 * Per-roster-bot LLM brain. Runs on the MAP thread from OnPlayerAfterUpdate,
 * one tick per bot. It is a trigger/cooldown/budget gate: it decides WHEN to
 * call the LLM, never WHAT the bot does per-GCD (combat stays 100% playerbots
 * engine).
 *
 * All network I/O is off-thread via LlmClient. Requests and completions carry
 * only ObjectGuid; the Player* is resolved at tick time from the hook argument,
 * so a logout can never dangle.
 */

#ifndef LLM_BOT_BRAIN_H
#define LLM_BOT_BRAIN_H

#include "LlmChatLog.h"
#include "LlmConversation.h"
#include "LlmEventBus.h"
#include "LlmTypes.h"

#include "ObjectGuid.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class Player;
class PlayerbotAI;

namespace LlmBots
{

class LlmBotRegistry;
class LlmClient;
class LlmBudget;

struct BrainStats
{
    uint32_t socialCalls = 0;
    uint32_t planCalls = 0;
    uint32_t replies = 0;
    uint32_t refused = 0;
    uint32_t fallenBack = 0; // responded with canned text after LLM failure
    uint32_t lastZoneId = 0;
    std::string goal;
};

class LlmBotBrain
{
public:
    LlmBotBrain(ObjectGuid guid, std::string name, std::string personaKey);

    void Configure(LlmClient* client, LlmBudget* budget, LlmBotRegistry* registry,
        LlmEventBus* bus, LlmConversationManager* conversations, LlmChatLog* chatLog,
        uint32_t socialCooldownSec, uint32_t planIntervalSec,
        std::string const& commanderName);

    // Called from the map thread for this bot. Never blocks.
    void Tick(Player* player, uint32_t pTimeMs);

    // Applies a finished request. Called from the map thread via the module.
    void ApplyCompletion(LlmRequest const& request, LlmResponse const& response);

    // Forced calls from .llmbot commands (map thread).
    void ForceSocial(Player* player, std::string const& prompt);
    void ForcePlan(Player* player);

    void Pause();
    void Resume();
    bool Paused() const;

    // Re-read persona/memory keys from the registry. Called on reload.
    void Refresh(LlmBotRegistry* registry);

    // Durable reflections from the DB, loaded by the module on reload.
    void SetMemories(std::vector<std::string> memories);

    ObjectGuid GetGuid() const { return m_guid; }
    std::string const& GetName() const { return m_name; }
    std::string const& GetGoal() const { return m_goal; }
    BrainStats GetStats() const;

private:
    void SubmitSocial(Player* player, LlmBotEvent const& event);
    void SubmitPlan(Player* player);
    void ApplyUtterance(Player* player, LlmResponse const& response, LlmBotEvent const& event);
    void ApplyAgenda(Player* player, LlmResponse const& response);
    void FallbackLine(Player* player, LlmBotEvent const& event);
    bool CooldownOk() const;
    void BuildContextSnapshot(Player* player, PlayerbotAI* ai);

    ObjectGuid m_guid;
    std::string m_name;
    std::string m_personaKey;

    mutable std::mutex m_mutex; // guards stats + goal, touched from map + world thread

    LlmClient* m_client = nullptr;
    LlmBudget* m_budget = nullptr;
    LlmBotRegistry* m_registry = nullptr;
    LlmEventBus* m_bus = nullptr;
    LlmConversationManager* m_conversations = nullptr;
    LlmChatLog* m_chatLog = nullptr;

    // Human commander whose messages may authorize commander-only tools.
    std::string m_commanderName = "CM";

    uint32_t m_socialCooldownSec = 20;
    uint32_t m_planIntervalSec = 300;

    std::chrono::steady_clock::time_point m_lastSocialAt;
    std::chrono::steady_clock::time_point m_lastPlanAt;
    std::atomic<bool> m_inFlight = false;

    bool m_paused = false;
    uint32_t m_tickAccumMs = 0;
    uint32_t m_tickCount = 0; // heartbeat counter (one log line per ~20 s per bot)

    // Context snapshot for prompts (refreshed each tick, cheap).
    std::string m_zone;
    std::string m_className;
    uint8_t m_level = 1;
    std::string m_healthFrac = "full";
    std::vector<std::string> m_memories;
    uint32_t m_zoneId = 0;

    // The event that triggered the in-flight social call (for the fallback
    // path and the reply target).
    LlmBotEvent m_pendingEvent;

    BrainStats m_stats;

    std::string m_goal; // last Tier 2 agenda decision, read by .llmbot list
};

} // namespace LlmBots

#endif // LLM_BOT_BRAIN_H
