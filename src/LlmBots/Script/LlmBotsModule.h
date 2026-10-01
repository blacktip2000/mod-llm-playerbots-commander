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
 * The module facade. Owns the Llm core, the registry, the brains, the event
 * bus and the chronicler, and exposes the entry points the script hooks call:
 *
 *   - OnWorldTick:      world thread - drains LLM completions, runs the roster
 *                       watchdog, logs budget stats.
 *   - OnPlayerTick:     map thread - one per roster bot per map update.
 *   - OnChat*:          chat events from our PlayerScript.
 *   - OnPlayerEvent*:   died / level-up / loot / duel.
 *   - command handlers: .llmbot
 */

#ifndef LLM_BOTS_MODULE_H
#define LLM_BOTS_MODULE_H

#include "LlmBotBrain.h"
#include "LlmTypes.h"

#include "ChroniclerBot.h"
#include "LlmBotRegistry.h"
#include "LlmConversation.h"
#include "LlmEventBus.h"

#include "DatabaseEnv.h"
#include "ObjectGuid.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

class Player;
class Item;

namespace LlmBots
{

class LlmBudget;
class LlmClient;

class LlmBotsModule
{
public:
    static LlmBotsModule* instance();

    // ---- lifecycle (module loader) ----
    void Initialize();
    void Shutdown();

    // ---- world thread ----
    void OnWorldTick(uint32_t diff);
    void ReloadRoster();

    // Applies the loaded roster (world thread, from the query processor).
    // Exposed for the unit-test harness.
    void OnRosterQueries(QueryResult rosterResult, QueryResult personaResult);

    // ---- map thread ----
    void OnPlayerTick(Player* player, uint32_t pTimeMs);

    // ---- event hooks (map or world thread) ----
    void OnPlayerLogin(Player* player);
    void OnPlayerLogout(Player* player);
    void OnWhisperToRosterBot(Player* sender, Player* bot, std::string const& msg);
    void OnSayOrYellNear(Player* sender, std::string const& msg, bool yell);
    void OnBotJustDied(Player* bot);
    void OnBotLevelUp(Player* bot);
    void OnBotLoot(Player* bot, Item* item);
    void OnDuelRequest(Player* target, Player* challenger);

    // ---- commands ----
    void CmdAdd(Player* gm, std::string const& name, std::string const& persona);
    void CmdRemove(Player* gm, std::string const& name);
    void CmdPersona(Player* gm, std::string const& name, std::string const& persona);
    void CmdStats(Player* gm);
    void CmdSay(Player* gm, std::string const& name, std::string const& prompt);
    void CmdGoal(Player* gm, std::string const& name);
    void CmdPause(Player* gm, bool paused);

    bool Enabled() const { return m_enabled; }
    bool Paused() const { return m_paused; }

    LlmBotRegistry& Registry() { return m_registry; }

private:
    void EnsureBotsSpawned();
    void ApplyMemoryForGuid(ObjectGuid guid, LlmBotBrain* brain);
    void BroadcastToGm(Player* gm, std::string const& text);

    LlmBotRegistry m_registry;
    LlmEventBus m_bus;
    LlmConversationManager m_conversations;
    LlmChatLog m_chatLog;

    std::unique_ptr<LlmBudget> m_budget;
    std::unique_ptr<LlmClient> m_client;

    // unique_ptr: LlmBotBrain holds a mutex + atomics (not movable), and the
    // roster is rebuilt wholesale on reload.
    std::map<ObjectGuid, std::unique_ptr<LlmBotBrain>> m_brains; // keyed by bot guid
    std::unique_ptr<ChroniclerBot> m_chronicler;
    ObjectGuid m_chroniclerGuid;

    bool m_enabled = false;
    bool m_paused = false;
    uint32_t m_spawnDelayMs = 60000; // let the world settle before spawning
    uint32_t m_watchdogAccumMs = 0;
    uint32_t m_statsAccumMs = 0;
    uint32_t m_tickLogCount = 0; // throttles the per-bot tick heartbeat log line
};

#define sLlmBotsModule LlmBotsModule::instance()

} // namespace LlmBots

#endif // LLM_BOTS_MODULE_H
