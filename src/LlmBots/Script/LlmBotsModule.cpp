/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 *
 * Modified in mod-playerbots-llm-commander:
 * Commander owner authorization and integration.
 * Modified version published on 2026-10-01.
 * Modification notice added on 2026-10-01.
 *
 * Based on bigr00/mod-llm-playerbots.
 * Original copyright and license notices remain in effect.
 */

#include "LlmBotsModule.h"
#include "PlayerbotsDatabase.h"

#include "LlmBotsConfig.h"
#include "LlmBudget.h"
#include "LlmClient.h"
#include "LlmProvider.h"
#include "LlmSchema.h"

#include "CharacterCache.h"
#include "Item.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "Script/WorldThr/PlayerbotOperations.h"
#include "Script/WorldThr/PlayerbotWorldThreadProcessor.h"
#include "SharedDefines.h"
#include "StringFormat.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"

#include <algorithm>

namespace LlmBots
{

namespace
{

uint32_t const kWatchdogIntervalMs = 10000;
uint32_t const kStatsIntervalMs = 60000;
float const kHearRangeYards = 30.0f;
char const* kPersonaDefaults[] = { "adventurer", "braggart", "scholar", "grump", "theorist" };

bool RosterActorOnline(ObjectGuid guid)
{
    return sRandomPlayerbotMgr.GetPlayerBot(guid) != nullptr;
}

bool CommanderAccountOnline()
{
    LlmBotsConfig const config = LoadLlmBotsConfig();
    if (!config.commanderAccountId)
    {
        return false;
    }

    WorldSession* session = sWorldSessionMgr->FindSession(config.commanderAccountId);
    return session && session->GetPlayer();
}

// Escapes a value for interpolation into plain SQL. Roster edits come from a
// GM command rather than an untrusted client, but a quote in a persona key or
// character name would still break the statement.
std::string Escaped(std::string const& value)
{
    std::string out = value;
    std::string escaped;
    escaped.reserve(out.size());
    for (char c : out)
    {
        if (c == '\\') escaped += "\\";
        else if (c == '\'') escaped += "''";
        else escaped += c;
    }
    out = std::move(escaped);
    return out;
}

} // namespace

LlmBotsModule* LlmBotsModule::instance()
{
    static LlmBotsModule module;
    return &module;
}

void LlmBotsModule::Initialize()
{
    LlmBotsConfig const config = LoadLlmBotsConfig();
    if (!config.enable)
    {
        LOG_INFO("llmbots.module", "LLM bots are disabled (LlmBots.Enable = 0)");
        return;
    }

    // Provider.
    LlmProvider* provider = CreateProvider(config.provider);
    if (!provider)
    {
        LOG_ERROR("llmbots.module", "Unknown provider '{}' - LLM bots disabled", config.provider);
        return;
    }

    ProviderConfig providerConfig;
    providerConfig.values["OpenaiEndpoint"] = config.openAiEndpoint;
    providerConfig.values["OpenaiApiKey"] = config.openAiApiKey;
    providerConfig.values["OpenaiModelFast"] = config.modelFast;
    providerConfig.values["OpenaiModelThink"] = config.modelThink;
    providerConfig.values["OpenaiTimeoutMs"] = std::to_string(config.openAiTimeoutMs);
    providerConfig.values["Endpoint"] = config.foundryEndpoint;
    providerConfig.values["ApiKey"] = config.foundryApiKey;
    providerConfig.values["DeploymentFast"] = config.deploymentFast;
    providerConfig.values["DeploymentThink"] = config.deploymentThink;
    providerConfig.values["TimeoutMs"] = std::to_string(config.foundryTimeoutMs);
    providerConfig.values["FailChance"] = std::to_string(config.mockFailChance);

    std::string providerErr;
    if (!provider->Configure(providerConfig, providerErr))
    {
        LOG_ERROR("llmbots.module", "Provider '{}' configuration failed: {} - LLM bots disabled", config.provider, providerErr);
        delete provider;
        return;
    }

    // Schema self-check before anything else - a strict-mode violation is a
    // startup log line, not a runtime 400.
    std::vector<std::string> const problems = ValidateSchemasForStrictMode();
    if (!problems.empty())
    {
        for (auto const& problem : problems)
        {
            LOG_ERROR("llmbots.module", "Schema strict-mode violation: {}", problem);
        }
        LOG_ERROR("llmbots.module", "Schemas must satisfy the strict-mode contract - LLM bots disabled");
        delete provider;
        return;
    }

    m_budget = std::make_unique<LlmBudget>();
    m_budget->Configure(config.maxCallsPerMinute, config.maxTokensPerHour, config.maxRosterSize * 2);

    m_client = std::make_unique<LlmClient>(std::unique_ptr<LlmProvider>(provider));
    m_client->Start(config.workerThreads, m_budget.get());

    m_conversations.Configure(config.conversationMaxTurns, /*maxConcurrent=*/3, /*pairCooldownSec=*/60);

    // Chronicler (may be empty - optional).
    if (!config.chroniclerName.empty())
    {
        m_chroniclerGuid = sCharacterCache->GetCharacterGuidByName(config.chroniclerName);
        m_chronicler = std::make_unique<ChroniclerBot>(config.chroniclerName, m_chroniclerGuid);
        m_chronicler->Configure(m_client.get(), &m_bus, &m_registry, config.chroniclerDigestSec);
    }

    m_enabled = true;
    m_paused = false;
    m_budget->SetEnabled(true);

    LOG_INFO("llmbots.module", "LLM bots initialized: provider={} workers={} roster limit={}",
        config.provider, config.workerThreads, config.maxRosterSize);

    ReloadRoster();
}

void LlmBotsModule::Shutdown()
{
    if (m_client)
    {
        m_client->Stop();
        m_client.reset();
    }
    m_budget.reset();
    m_brains.clear();
    m_chronicler.reset();
    m_enabled = false;
}

void LlmBotsModule::ReloadRoster()
{
    if (!m_enabled)
    {
        return;
    }
    // Blocking queries on the world thread - startup/reload only, and the
    // playerbots module itself uses blocking queries throughout.
    //
    // Plain SQL rather than prepared statements on purpose: prepared
    // statements would have to be registered in the core's
    // PlayerbotsDatabaseConnection, which would make this feature a core
    // patch. These are fixed literals over the module's own tables, so there
    // is nothing to bind.
    OnRosterQueries(
        PlayerbotsDatabase.Query("SELECT name, persona, bot_type, enabled FROM llm_bot_roster"),
        PlayerbotsDatabase.Query(
            "SELECT persona, display_name, brief, voice, quirks, self_awareness FROM llm_bot_persona"));
}

void LlmBotsModule::OnRosterQueries(QueryResult rosterResult, QueryResult personaResult)
{
    m_registry.ApplyFromQueries(std::move(rosterResult), std::move(personaResult));

    // Rebuild brains for the current roster.
    std::vector<RosterEntry> const entries = m_registry.Entries(/*actorsOnly=*/false);
    std::map<ObjectGuid, std::unique_ptr<LlmBotBrain>> newBrains;
    for (auto const& entry : entries)
    {
        if (entry.botType == BotType::Chronicler)
        {
            continue;
        }
        auto itr = m_brains.find(entry.guid);
        if (itr != m_brains.end())
        {
            newBrains.emplace(entry.guid, std::move(itr->second));
            newBrains.at(entry.guid)->Refresh(&m_registry);
        }
        else
        {
            auto brain = std::make_unique<LlmBotBrain>(entry.guid, entry.name, entry.personaKey);
            LlmBotsConfig const config = LoadLlmBotsConfig();
            brain->Configure(m_client.get(), m_budget.get(), &m_registry, &m_bus, &m_conversations,
                &m_chatLog, config.socialCooldownSec, config.planIntervalSec, config.commanderName);
            ApplyMemoryForGuid(entry.guid, brain.get());
            newBrains.emplace(entry.guid, std::move(brain));
        }
    }
    m_brains = std::move(newBrains);

    LOG_INFO("llmbots.module", "Roster applied: {} brains active", m_brains.size());
}

void LlmBotsModule::ApplyMemoryForGuid(ObjectGuid guid, LlmBotBrain* brain)
{
    // Blocking query on the world thread - only at startup/reload, and the
    // playerbots module itself uses blocking queries throughout.
    std::vector<std::string> memories;
    if (QueryResult result = PlayerbotsDatabase.Query(
            "SELECT kind, subject, text FROM llm_bot_memory WHERE guid = {} ORDER BY created_at DESC LIMIT 20",
            guid.GetCounter()))
    {
        do
        {
            Field* fields = result->Fetch();
            std::string const kind = fields[0].Get<std::string>();
            std::string const subject = fields[1].Get<std::string>();
            std::string const text = fields[2].Get<std::string>();
            if (kind == "event")
            {
                continue; // events are transient; only durable lines go in prompts
            }
            if (subject.empty())
            {
                memories.push_back(text);
            }
            else
            {
                memories.push_back(subject + ": " + text);
            }
        } while (result->NextRow());
    }
    brain->SetMemories(std::move(memories));
}

void LlmBotsModule::OnWorldTick(uint32_t diff)
{
    if (!m_enabled)
    {
        return;
    }

    // LLM completions -> route to the right brain / chronicler.
    std::vector<LlmCompletion> completions;
    m_client->DrainCompletions(completions);
    for (auto const& completion : completions)
    {
        if (completion.request.purpose == Purpose::Chronicler && m_chronicler)
        {
            m_chronicler->ApplyCompletion(completion.response);
        }
        else
        {
            ObjectGuid const requester = ObjectGuid::Create<HighGuid::Player>(completion.request.requesterId);
            auto const itr = m_brains.find(requester);
            if (itr != m_brains.end())
            {
                itr->second->ApplyCompletion(completion.request, completion.response);
            }
        }
    }

    // Spawn / watchdog.
    m_watchdogAccumMs += diff;
    if (m_watchdogAccumMs >= kWatchdogIntervalMs)
    {
        m_watchdogAccumMs = 0;
        EnsureBotsSpawned();
    }

    // Budget stats log.
    m_statsAccumMs += diff;
    if (m_statsAccumMs >= kStatsIntervalMs)
    {
        m_statsAccumMs = 0;
        if (m_budget)
        {
            BudgetStats const stats = m_budget->GetStats();
            LOG_INFO("llmbots.budget", "calls={} refused={} errors={} fallbacks={} tokens={}+{} hour={} circuit={}",
                stats.calls, stats.refused, stats.errors, stats.fallbacks,
                stats.tokensPrompt, stats.tokensCompletion,
                m_budget->TokensThisHour(), m_client->IsCircuitOpen());
        }
    }
}

void LlmBotsModule::OnPlayerLogin(Player* player)
{
    if (!m_enabled || !player || !player->GetSession())
    {
        return;
    }

    LlmBotsConfig const config = LoadLlmBotsConfig();
    if (!config.commanderAccountId ||
        player->GetSession()->GetAccountId() != config.commanderAccountId)
    {
        return;
    }

    LOG_INFO("llmbots.module", "Commander account {} entered the world - spawning LLM roster",
        config.commanderAccountId);

    m_spawnDelayMs = 0;
    EnsureBotsSpawned();
}

void LlmBotsModule::OnPlayerLogout(Player* player)
{
    if (!m_enabled || !player || !player->GetSession())
    {
        return;
    }

    LlmBotsConfig const config = LoadLlmBotsConfig();
    if (!config.commanderAccountId ||
        player->GetSession()->GetAccountId() != config.commanderAccountId)
    {
        return;
    }

    LOG_INFO("llmbots.module", "Commander account {} left the world - logging out LLM roster",
        config.commanderAccountId);

    for (auto const& entry : m_registry.Entries(/*actorsOnly=*/false))
    {
        if (RosterActorOnline(entry.guid))
        {
            sRandomPlayerbotMgr.LogoutPlayerBot(entry.guid);
        }
    }
}

void LlmBotsModule::EnsureBotsSpawned()
{
    // LLM roster exists only while the configured owner account
    // has a character actually inside the world.
    if (!CommanderAccountOnline())
    {
        return;
    }

    // Initial spawn is delayed so the world has settled after startup.
    if (m_spawnDelayMs > 0)
    {
        if (m_spawnDelayMs > kWatchdogIntervalMs)
        {
            m_spawnDelayMs -= kWatchdogIntervalMs;
            return;
        }
        m_spawnDelayMs = 0;
    }

    for (auto const& entry : m_registry.Entries(/*actorsOnly=*/false))
    {
        if (entry.botType == BotType::Chronicler)
        {
            // The chronicler is an observer, not an actor, but it must be
            // logged in as a character to receive whispers.
            if (!RosterActorOnline(entry.guid))
            {
                PlayerbotWorldThreadProcessor::instance().QueueOperation(
                    std::make_unique<AddPlayerBotOperation>(entry.guid, /*masterAccountId=*/0));
                LOG_DEBUG("llmbots.module", "Re-queued spawn for chronicler {}", entry.name);
            }
            continue;
        }
        if (!RosterActorOnline(entry.guid))
        {
            PlayerbotWorldThreadProcessor::instance().QueueOperation(
                std::make_unique<AddPlayerBotOperation>(entry.guid, /*masterAccountId=*/0));
            LOG_DEBUG("llmbots.module", "Re-queued spawn for roster bot {}", entry.name);
        }
    }
}

void LlmBotsModule::OnPlayerTick(Player* player, uint32_t pTimeMs)
{
    if (!m_enabled || m_paused)
    {
        return;
    }
    auto const itr = m_brains.find(player->GetGUID());
    if (itr == m_brains.end())
    {
        return;
    }
    // Heartbeat, throttled: one line per ~50 s. Proves the hook fires and the
    // brain lookup hits, without flooding at tick rate (19 bots * 20 Hz would
    // be hundreds of lines a second).
    if (++m_tickLogCount % 1000 == 0)
    {
        LOG_DEBUG("llmbots.brain", "OnPlayerTick: {} pTime={}ms paused={}", player->GetName(), pTimeMs, m_paused);
    }
    itr->second->Tick(player, pTimeMs);
}

void LlmBotsModule::OnWhisperToRosterBot(Player* sender, Player* bot, std::string const& msg)
{
    if (!m_enabled || m_paused || !sender || !bot)
    {
        return;
    }

    LlmBotsConfig const config = LoadLlmBotsConfig();

    auto equalsIgnoreCaseAscii = [](std::string const& left, std::string const& right)
    {
        if (left.size() != right.size())
        {
            return false;
        }

        for (size_t i = 0; i < left.size(); ++i)
        {
            char const a = (left[i] >= 'A' && left[i] <= 'Z') ? left[i] + ('a' - 'A') : left[i];
            char const b = (right[i] >= 'A' && right[i] <= 'Z') ? right[i] + ('a' - 'A') : right[i];
            if (a != b)
            {
                return false;
            }
        }
        return true;
    };

    bool const isCommander = equalsIgnoreCaseAscii(bot->GetName(), config.commanderName);
    if (isCommander)
    {
        WorldSession* senderSession = sender->GetSession();
        if (!senderSession || !config.commanderAccountId ||
            senderSession->GetAccountId() != config.commanderAccountId)
        {
            LOG_DEBUG("llmbots.brain",
                "whisper to commander '{}' ignored: unauthorized account",
                bot->GetName());
            return;
        }
    }

    if (m_chronicler && bot->GetGUID() == m_chroniclerGuid)
    {
        m_chronicler->OnWhisper(sender, msg);
        return;
    }
    if (m_brains.count(bot->GetGUID()) == 0)
    {
        LOG_DEBUG("llmbots.brain", "whisper to '{}' ignored: no brain for guid {}", bot->GetName(), bot->GetGUID().GetCounter());
        return;
    }

    ChatLine line;
    line.speaker = sender->GetName();
    line.text = msg;
    line.channel = "whisper";
    line.gameTimeSec = m_bus.NowGameTime();
    m_chatLog.Push(bot->GetGUID(), std::move(line));

    LlmBotEvent event;
    event.kind = EventKind::Whisper;
    event.guid = bot->GetGUID();
    event.actor = sender->GetGUID();
    event.subject = sender->GetName();
    event.detail = msg;
    m_bus.Push(event);
    LOG_DEBUG("llmbots.brain", "whisper '{}' -> '{}' queued for brain", sender->GetName(), bot->GetName());
}

void LlmBotsModule::OnSayOrYellNear(Player* sender, std::string const& msg, bool /*yell*/)
{
    if (!m_enabled || m_paused || !sender || msg.empty())
    {
        return;
    }

    bool const senderIsRosterBot = m_registry.FindByGuid(sender->GetGUID()) != nullptr;
    for (auto const& entry : m_registry.Entries(/*actorsOnly=*/true))
    {
        if (entry.guid == sender->GetGUID())
        {
            continue;
        }
        Player* bot = sRandomPlayerbotMgr.GetPlayerBot(entry.guid);
        if (!bot || !bot->IsAlive())
        {
            continue;
        }
        if (bot->GetMapId() != sender->GetMapId())
        {
            continue;
        }
        if (bot->GetDistance2d(sender) > kHearRangeYards)
        {
            continue;
        }

        ChatLine line;
        line.speaker = sender->GetName();
        line.text = msg;
        line.channel = "say";
        line.gameTimeSec = m_bus.NowGameTime();
        m_chatLog.Push(entry.guid, std::move(line));

        LlmBotEvent event;
        event.kind = senderIsRosterBot ? EventKind::BotSayNear : EventKind::SayNear;
        event.guid = entry.guid;
        event.actor = sender->GetGUID();
        event.subject = sender->GetName();
        event.detail = msg;
        m_bus.Push(event);
    }
}

void LlmBotsModule::OnBotJustDied(Player* bot)
{
    if (!m_enabled || m_paused)
    {
        return;
    }
    if (m_brains.count(bot->GetGUID()) == 0)
    {
        return;
    }
    LlmBotEvent event;
    event.kind = EventKind::Died;
    event.guid = bot->GetGUID();
    m_bus.Push(event);
}

void LlmBotsModule::OnBotLevelUp(Player* bot)
{
    if (!m_enabled || m_paused)
    {
        return;
    }
    if (m_brains.count(bot->GetGUID()) == 0)
    {
        return;
    }
    LlmBotEvent event;
    event.kind = EventKind::LevelUp;
    event.guid = bot->GetGUID();
    m_bus.Push(event);
}

void LlmBotsModule::OnBotLoot(Player* bot, Item* item)
{
    if (!m_enabled || m_paused || !item)
    {
        return;
    }
    if (m_brains.count(bot->GetGUID()) == 0)
    {
        return;
    }
    if (item->GetTemplate()->Quality < ITEM_QUALITY_RARE)
    {
        return; // not worth a line for grey/green junk
    }
    LlmBotEvent event;
    event.kind = EventKind::LootItem;
    event.guid = bot->GetGUID();
    event.subject = item->GetTemplate()->Name1;
    event.detail = "";
    m_bus.Push(event);
}

void LlmBotsModule::OnDuelRequest(Player* target, Player* challenger)
{
    if (!m_enabled || m_paused)
    {
        return;
    }
    if (m_brains.count(target->GetGUID()) == 0)
    {
        return;
    }
    LlmBotEvent event;
    event.kind = EventKind::DuelRequest;
    event.guid = target->GetGUID();
    event.subject = challenger->GetName();
    event.detail = "";
    m_bus.Push(event);
}

void LlmBotsModule::BroadcastToGm(Player* gm, std::string const& text)
{
    if (gm && gm->GetSession())
    {
        gm->GetSession()->SendAreaTriggerMessage(text.c_str());
    }
}

// ---- commands ----

void LlmBotsModule::CmdAdd(Player* gm, std::string const& name, std::string const& persona)
{
    if (!m_enabled)
    {
        BroadcastToGm(gm, "LLM bots are disabled");
        return;
    }
    if (name.empty() || m_registry.Count() >= LoadLlmBotsConfig().maxRosterSize)
    {
        BroadcastToGm(gm, "Roster full or empty name");
        return;
    }
    ObjectGuid const guid = sCharacterCache->GetCharacterGuidByName(name);
    if (guid.IsEmpty())
    {
        BroadcastToGm(gm, "No such character");
        return;
    }
    if (m_registry.FindByGuid(guid))
    {
        BroadcastToGm(gm, "Already on the roster");
        return;
    }

    std::string const personaKey = persona.empty() ? kPersonaDefaults[guid.GetCounter() % 5] : persona;
    PlayerbotsDatabase.Execute(
        "INSERT INTO llm_bot_roster (name, persona, bot_type, enabled) VALUES ('{}', '{}', {}, 1)",
        Escaped(name), Escaped(personaKey), static_cast<uint32_t>(BotType::Actor));
    ReloadRoster();
    BroadcastToGm(gm, name + " added to the roster");
}

void LlmBotsModule::CmdRemove(Player* gm, std::string const& name)
{
    if (!m_enabled)
    {
        BroadcastToGm(gm, "LLM bots are disabled");
        return;
    }
    if (!m_registry.FindByName(name))
    {
        BroadcastToGm(gm, "Not on the roster");
        return;
    }
    PlayerbotsDatabase.Execute("DELETE FROM llm_bot_roster WHERE name = '{}'", Escaped(name));
    ReloadRoster();
    BroadcastToGm(gm, name + " removed from the roster");
}

void LlmBotsModule::CmdPersona(Player* gm, std::string const& name, std::string const& persona)
{
    if (!m_enabled)
    {
        BroadcastToGm(gm, "LLM bots are disabled");
        return;
    }
    if (!m_registry.FindPersona(persona))
    {
        BroadcastToGm(gm, "No such persona '" + persona + "'");
        return;
    }
    PlayerbotsDatabase.Execute("UPDATE llm_bot_roster SET persona = '{}' WHERE name = '{}'",
        Escaped(persona), Escaped(name));
    ReloadRoster();
    BroadcastToGm(gm, name + " now uses persona '" + persona + "'");
}

void LlmBotsModule::CmdStats(Player* gm)
{
    if (!m_enabled || !m_budget)
    {
        BroadcastToGm(gm, "LLM bots are disabled");
        return;
    }
    BudgetStats const stats = m_budget->GetStats();
    uint32_t const callsPerMin = stats.latencyCount ? static_cast<uint32_t>(stats.latencyTotalMs / stats.latencyCount) : 0;
    BroadcastToGm(gm, Acore::StringFormat(
        "LLM bots: roster={} brains={} calls={} refused={} errors={} fallbacks={} tokens={}+{} this hour={} avgMs={} circuit={}",
        m_registry.Count(), m_brains.size(), stats.calls, stats.refused, stats.errors, stats.fallbacks,
        stats.tokensPrompt, stats.tokensCompletion, m_budget->TokensThisHour(), callsPerMin, m_client->IsCircuitOpen()).c_str());
}

void LlmBotsModule::CmdSay(Player* gm, std::string const& name, std::string const& prompt)
{
    RosterEntry const* entry = m_registry.FindByName(name);
    if (!entry)
    {
        BroadcastToGm(gm, "Not on the roster");
        return;
    }
    Player* bot = ObjectAccessor::FindPlayer(entry->guid);
    if (!bot)
    {
        BroadcastToGm(gm, "Bot is offline");
        return;
    }
    auto const itr = m_brains.find(entry->guid);
    if (itr != m_brains.end())
    {
        itr->second->ForceSocial(bot, prompt);
        BroadcastToGm(gm, "Submitted");
    }
}

void LlmBotsModule::CmdGoal(Player* gm, std::string const& name)
{
    RosterEntry const* entry = m_registry.FindByName(name);
    if (!entry)
    {
        BroadcastToGm(gm, "Not on the roster");
        return;
    }
    Player* bot = ObjectAccessor::FindPlayer(entry->guid);
    if (!bot)
    {
        BroadcastToGm(gm, "Bot is offline");
        return;
    }
    auto const itr = m_brains.find(entry->guid);
    if (itr != m_brains.end())
    {
        itr->second->ForcePlan(bot);
        BroadcastToGm(gm, "Submitted");
    }
}

void LlmBotsModule::CmdPause(Player* gm, bool paused)
{
    m_paused = paused;
    BroadcastToGm(gm, paused ? "LLM bots paused - all bots degrade to plain playerbots" : "LLM bots resumed");
}

} // namespace LlmBots
