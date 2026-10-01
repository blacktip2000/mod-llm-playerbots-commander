/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 *
 * Modified in mod-playerbots-llm-commander:
 * Commander-specific action tools and response handling.
 * Modified version published on 2026-10-01.
 * Modification notice added on 2026-10-01.
 *
 * Based on bigr00/mod-llm-playerbots.
 * Original copyright and license notices remain in effect.
 */

#include "LlmBotBrain.h"

#include "LlmBotRegistry.h"
#include "LlmBudget.h"
#include "LlmClient.h"
#include "LlmToolbox.h"
#include "PromptBuilder.h"

#include "ExternalEventHelper.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Playerbots.h"
#include "SharedDefines.h"

#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <limits>

using nlohmann::json;

namespace LlmBots
{

namespace
{

uint32_t const kTickIntervalMs = 500;

// How long a bot waits before retrying a plan call the budget refused.
uint32_t const kPlanRetryBackoffSec = 30;

// How many tool round-trips one utterance may take before the bot has to
// answer with what it has. Each round is another API call.
uint32_t const kMaxToolRounds = 4;

// True for events a player is directly waiting on. Only these are worth a
// canned line when the budget refuses the call; canned banter is just noise.
bool IsPlayerDirected(EventKind kind)
{
    return kind == EventKind::Whisper || kind == EventKind::SayNear || kind == EventKind::DuelRequest;
}

char const* kClassNameForClass[12] = { "", "Warrior", "Paladin", "Hunter", "Rogue", "Priest",
    "Death Knight", "Shaman", "Mage", "Warlock", "", "Druid" };

// Fallback lines when the LLM fails or is refused. The bot always says
// something - it just isn't clever. Indexed by EventKind.
std::vector<std::string> const kFallbackLines = {
    "Hm.",                                  // Whisper
    "That's fair.",                         // SayNear
    "Eh.",                                  // BotSayNear
    "Well. That happened.",                 // Died
    "Another step closer.",                 // LevelUp
    "Fine, let's see what you've got.",     // DuelRequest
    "Not bad.",                             // LootItem
    "Quiet around here, isn't it?",         // ZoneChanged
    "Onwards."                              // GoalCompleted
};

// WoW character names are normalized by the server (for example "CM" may
// become "Cm"), so commander-name matching must be case-insensitive.
bool EqualsIgnoreCase(std::string const& left, std::string const& right)
{
    if (left.size() != right.size())
    {
        return false;
    }

    return std::equal(left.begin(), left.end(), right.begin(),
        [](unsigned char a, unsigned char b)
        {
            return std::tolower(a) == std::tolower(b);
        });
}

uint32_t EmoteIdFor(std::string const& emote)
{
    if (emote == "laugh") return EMOTE_ONESHOT_LAUGH;
    if (emote == "cheer") return EMOTE_ONESHOT_CHEER;
    if (emote == "cry") return EMOTE_ONESHOT_CRY;
    if (emote == "rude") return EMOTE_ONESHOT_RUDE;
    if (emote == "point") return EMOTE_ONESHOT_POINT;
    if (emote == "salute") return EMOTE_ONESHOT_SALUTE;
    if (emote == "wave") return EMOTE_ONESHOT_WAVE;
    if (emote == "bow") return EMOTE_ONESHOT_BOW;
    if (emote == "question") return EMOTE_ONESHOT_QUESTION;
    if (emote == "applaud") return EMOTE_ONESHOT_APPLAUD;
    if (emote == "flex") return EMOTE_ONESHOT_FLEX;
    if (emote == "shy") return EMOTE_ONESHOT_SHY;
    return 0;
}

// WoW chat drops messages over 255 bytes and the client truncates anything
// longer. Cap by characters for style, then by bytes on a UTF-8 boundary so
// no multibyte character is ever split mid-sequence.
void ClampLine(std::string& line)
{
    if (line.size() > 160)
    {
        line.resize(160);
    }
    uint32_t constexpr kMaxLineBytes = 240;
    if (line.size() > kMaxLineBytes)
    {
        line.resize(kMaxLineBytes);
        while (!line.empty() && (static_cast<uint8_t>(line.back()) & 0xC0) == 0x80)
        {
            line.pop_back();
        }
        if (!line.empty())
        {
            line.pop_back(); // drop the incomplete lead byte
        }
    }
}

} // namespace

LlmBotBrain::LlmBotBrain(ObjectGuid guid, std::string name, std::string personaKey)
    : m_guid(guid), m_name(std::move(name)), m_personaKey(std::move(personaKey))
{
    // Never seed these with time_point::min(): steady_clock::duration is a
    // 64-bit nanosecond count, so (now - min()) overflows and the elapsed
    // comparisons below wrap negative - the gates would never open. Seed with
    // now instead; Configure() re-seeds once the intervals are known.
    auto const now = std::chrono::steady_clock::now();
    m_lastSocialAt = now;
    m_lastPlanAt = now;
}

void LlmBotBrain::Configure(LlmClient* client, LlmBudget* budget, LlmBotRegistry* registry,
    LlmEventBus* bus, LlmConversationManager* conversations, LlmChatLog* chatLog,
    uint32_t socialCooldownSec, uint32_t planIntervalSec,
    std::string const& commanderName)
{
    m_client = client;
    m_budget = budget;
    m_registry = registry;
    m_bus = bus;
    m_conversations = conversations;
    m_chatLog = chatLog;
    m_socialCooldownSec = socialCooldownSec;
    m_planIntervalSec = planIntervalSec;
    m_commanderName = commanderName;

    // Open the social gate straight away, and stagger the first plan call
    // across one full interval so a 20-bot roster does not fire 20 Tier 2
    // calls in the same second and blow MaxCallsPerMinute. The offset is
    // derived from the guid, so it is stable across restarts.
    auto const now = std::chrono::steady_clock::now();
    m_lastSocialAt = now - std::chrono::seconds(m_socialCooldownSec);
    if (m_planIntervalSec > 0)
    {
        uint32_t const offset = m_guid.GetCounter() % m_planIntervalSec;
        m_lastPlanAt = now - std::chrono::seconds(m_planIntervalSec) + std::chrono::seconds(offset);
    }
    else
    {
        m_lastPlanAt = now;
    }
}

void LlmBotBrain::Refresh(LlmBotRegistry* registry)
{
    m_registry = registry;
}

void LlmBotBrain::SetMemories(std::vector<std::string> memories)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_memories = std::move(memories);
}

void LlmBotBrain::Pause()
{
    m_paused = true;
}

void LlmBotBrain::Resume()
{
    m_paused = false;
}

bool LlmBotBrain::Paused() const
{
    return m_paused;
}

BrainStats LlmBotBrain::GetStats() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    BrainStats stats = m_stats;
    stats.goal = m_goal;
    stats.lastZoneId = m_zoneId;
    return stats;
}

void LlmBotBrain::Tick(Player* player, uint32_t pTimeMs)
{
    // Heartbeat: one line per ~50 s per bot, so a dead pipeline is visible
    // instead of silent. Shows the gate state at the moment of the tick.
    if (++m_tickCount % 1000 == 0)
    {
        auto const now = std::chrono::steady_clock::now();
        LOG_DEBUG("llmbots.brain",
            "{}: tick heartbeat paused={} client={} accum={}ms inFlight={} planInterval={}s elapsed={}s",
            m_name, m_paused, m_client != nullptr, m_tickAccumMs, m_inFlight.load(),
            m_planIntervalSec,
            std::chrono::duration_cast<std::chrono::seconds>(now - m_lastPlanAt).count());
    }
    if (m_paused || !m_client)
    {
        return;
    }

    m_tickAccumMs += pTimeMs;
    if (m_tickAccumMs < kTickIntervalMs)
    {
        return;
    }
    m_tickAccumMs = 0;

    PlayerbotAI* ai = GET_PLAYERBOT_AI(player);
    if (!ai)
    {
        // Throttled: one line per ~50 s. Distinguishes "hook dead" from
        // "AI detached" at a glance.
        if (m_tickCount % 1000 == 0)
        {
            LOG_DEBUG("llmbots.brain", "{}: tick blocked: no PlayerbotAI (config.enabled={})", m_name,
                sPlayerbotAIConfig.enabled);
        }
        return;
    }

    // Zone change detection - fires a Tier 1 observation on arrival somewhere
    // notable. Cheap: one area id compare per tick per roster bot.
    uint32_t const zoneId = ai->GetCurrentZone() ? ai->GetCurrentZone()->ID : 0u;
    if (zoneId != m_zoneId)
    {
        if (m_zoneId != 0 && zoneId != 0)
        {
            LlmBotEvent zoneEvent;
            zoneEvent.kind = EventKind::ZoneChanged;
            zoneEvent.guid = m_guid;
            zoneEvent.subject = ai->GetLocalizedAreaName(ai->GetCurrentZone());
            m_bus->Push(zoneEvent);
        }
        m_zoneId = zoneId;
        m_zone = zoneId != 0 ? ai->GetLocalizedAreaName(ai->GetCurrentZone()) : "";
    }

    bool const isCommander = EqualsIgnoreCase(m_name, m_commanderName);

    // One event per tick interval - the cadence gate is per bot, the global
    // rate gate is inside LlmBudget::Check.
    //
    // CM is intentionally whisper-only. Ambient/world events may still be
    // emitted by shared hooks, but the commander brain discards them here.
    if (!m_inFlight.load())
    {
        uint32_t const maxPriority = CooldownOk() ? std::numeric_limits<uint32_t>::max() : 0;

        LlmBotEvent event;
        while (m_bus->TakeBestFor(m_guid, event, maxPriority))
        {
            if (isCommander && event.kind != EventKind::Whisper)
            {
                continue;
            }

            // Bot-to-bot banter goes through the conversation guard for
            // ordinary conversational LLM bots.
            if (event.kind == EventKind::BotSayNear && m_conversations &&
                !m_conversations->MayReply(m_guid, event.actor))
            {
                continue;
            }

            LOG_DEBUG("llmbots.brain", "{}: handling event kind {} from '{}'",
                m_name, static_cast<int>(event.kind), event.subject);
            SubmitSocial(player, event);
            break;
        }
    }

    // Tier 2 autonomous planning is for conversational/adventuring bots only.
    // CM acts solely in response to an authorized whisper.
    if (!isCommander && !m_inFlight.load())
    {
        auto const now = std::chrono::steady_clock::now();
        if (m_planIntervalSec > 0 && now - m_lastPlanAt >= std::chrono::seconds(m_planIntervalSec))
        {
            LOG_DEBUG("llmbots.brain", "{}: plan gate open (interval {}s, elapsed {}s), submitting",
                m_name, m_planIntervalSec,
                std::chrono::duration_cast<std::chrono::seconds>(now - m_lastPlanAt).count());
            SubmitPlan(player);
        }
    }
}

void LlmBotBrain::ForceSocial(Player* player, std::string const& prompt)
{
    if (m_inFlight.load())
    {
        return;
    }
    LlmBotEvent event;
    event.kind = EventKind::SayNear;
    event.guid = m_guid;
    event.subject = "the command";
    event.detail = prompt;
    SubmitSocial(player, event);
}

void LlmBotBrain::ForcePlan(Player* player)
{
    if (m_inFlight.load())
    {
        return;
    }
    SubmitPlan(player);
}

bool LlmBotBrain::CooldownOk() const
{
    return std::chrono::steady_clock::now() - m_lastSocialAt >= std::chrono::seconds(m_socialCooldownSec);
}

void LlmBotBrain::BuildContextSnapshot(Player* player, PlayerbotAI* /*ai*/)
{
    uint8_t const cls = player->getClass();
    m_className = cls < 12 ? kClassNameForClass[cls] : "";
    m_level = player->GetLevel();
    m_healthFrac = "full";
    if (player->IsAlive())
    {
        float const pct = static_cast<float>(player->GetHealth()) / static_cast<float>(player->GetMaxHealth());
        m_healthFrac = pct < 0.25f ? "critical" : (pct < 0.7f ? "hurt" : "full");
    }
    else
    {
        m_healthFrac = "dead";
    }
}

void LlmBotBrain::SubmitSocial(Player* player, LlmBotEvent const& event)
{
    PlayerbotAI* ai = GET_PLAYERBOT_AI(player);
    if (!ai)
    {
        return;
    }
    BuildContextSnapshot(player, ai);

    // Commander.Name identifies the LLM bot that owns command authority.
    // Ordinary roster bots may converse, but only this brain receives
    // action/command tools.
    bool const isCommander = EqualsIgnoreCase(m_name, m_commanderName);

    if (isCommander)
    {
        LOG_DEBUG("llmbots.brain", "{}: commander brain recognized (speaker '{}')", m_name, event.subject);
    }

    Persona const* persona = m_registry ? m_registry->FindPersona(m_personaKey) : nullptr;
    Persona const defaultPersona = { m_name, m_name, "An adventurer like any other.", "plain, honest speech.",
        "Occasionally cracks a dry joke.", 0 };

    BotContext context;
    context.name = m_name;
    context.className = m_className;
    context.level = m_level;
    context.zone = m_zone;
    context.healthFrac = m_healthFrac;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        context.goal = m_goal;
        context.memories = m_memories;
    }

    LlmRequest request;
    request.tier = ModelTier::Fast;
    request.purpose = Purpose::Social;
    request.schemaName = "bot_utterance";
    request.maxOutputTokens = 800;
    request.requesterId = m_guid.GetCounter();

    if (isCommander)
    {
        // CM is a private tool interface, not a role-playing adventurer.
        request.temperature = 0.1f;
        request.maxOutputTokens = 400;
        request.tools = CommanderToolSpecs();

        request.system =
            "You are CM, a private command interface for the authorized human owner. "
            "You are not an adventurer, companion, NPC, or role-playing character. "
            "Do not invent a personality, emotions, memories, desires, jokes, or autonomous goals. "
            "Keep replies concise and functional. "
            "For any request that asks you to command a bot or change game state, use an available tool. "
            "Never claim that an action was performed unless a tool result confirms it. "
            "If no available tool can perform the requested action, say that the action is not available yet. "
            "Do not pretend to execute unsupported actions.";

        request.user = event.detail;

        LOG_DEBUG("llmbots.brain", "{}: commander-only tools enabled for '{}'", m_name, event.subject);
    }
    else
    {
        request.tools = SocialToolSpecs();
        request.system = BuildSystemPrompt(persona ? *persona : defaultPersona, context, ModelTier::Fast);
        request.user = BuildSocialPrompt(event, context);
    }

    LOG_DEBUG("llmbots.brain", "{}: submitting social call (event kind {})", m_name, static_cast<int>(event.kind));
    if (event.kind == EventKind::Whisper)
    {
        LOG_DEBUG("llmbots.brain", "{}: whisper reply target='{}' text='{}'", m_name, event.subject, event.detail);
    }
    if (!m_client->Submit(request, nullptr))
    {
        LOG_DEBUG("llmbots.brain", "{}: social submit refused (event kind {})", m_name, static_cast<int>(event.kind));
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            ++m_stats.refused;
        }

        // A refusal is the budget saying "not now". Start the cooldown anyway,
        // otherwise the bot retries on the very next tick and burns its whole
        // drained event queue against a budget that is already empty.
        m_lastSocialAt = std::chrono::steady_clock::now();

        // Only answer a human with a canned line. Falling back on bot banter
        // fills the chat with "Eh." every time the budget is tight.
        if (IsPlayerDirected(event.kind))
        {
            FallbackLine(player, event);
        }
        return;
    }

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_pendingEvent = event;
        ++m_stats.socialCalls;
    }
    m_lastSocialAt = std::chrono::steady_clock::now();
    m_inFlight.store(true);
}

void LlmBotBrain::SubmitPlan(Player* player)
{
    PlayerbotAI* ai = GET_PLAYERBOT_AI(player);
    if (!ai)
    {
        return;
    }
    BuildContextSnapshot(player, ai);

    Persona const* persona = m_registry ? m_registry->FindPersona(m_personaKey) : nullptr;
    Persona const defaultPersona = { m_name, m_name, "An adventurer like any other.", "plain, honest speech.",
        "Occasionally cracks a dry joke.", 0 };

    BotContext context;
    context.name = m_name;
    context.className = m_className;
    context.level = m_level;
    context.zone = m_zone;
    context.healthFrac = m_healthFrac;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        context.goal = m_goal;
        context.memories = m_memories;
    }

    LlmRequest request;
    request.tier = ModelTier::Think;
    request.purpose = Purpose::Plan;
    request.schemaName = "bot_agenda";
    request.maxOutputTokens = 256;
    request.requesterId = m_guid.GetCounter();
    request.temperature = 0.7f;
    request.system = BuildSystemPrompt(persona ? *persona : defaultPersona, context, ModelTier::Think);
    request.user = BuildPlanPrompt(context);

    LOG_DEBUG("llmbots.brain", "{}: submitting plan call", m_name);
    if (!m_client->Submit(request, nullptr))
    {
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            ++m_stats.refused;
        }

        // Same rule as SubmitSocial: a refused call still closes the gate, or
        // the plan branch reopens on every tick and hammers a spent budget.
        // Back off a fraction of the interval rather than the full one, so a
        // plan lost to a busy minute is retried well before the next cycle.
        m_lastPlanAt = std::chrono::steady_clock::now() -
            std::chrono::seconds(m_planIntervalSec > kPlanRetryBackoffSec ? m_planIntervalSec - kPlanRetryBackoffSec : 0);
        return;
    }

    m_lastPlanAt = std::chrono::steady_clock::now();
    m_inFlight.store(true);
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        ++m_stats.planCalls;
    }
}

void LlmBotBrain::ApplyCompletion(LlmRequest const& request, LlmResponse const& response)
{
    m_inFlight.store(false);

    // Resolve the bot. The bot may have logged out or been removed from the
    // roster while the request was in flight - drop silently then.
    Player* player = ObjectAccessor::FindPlayer(m_guid);
    if (!player)
    {
        return;
    }
    PlayerbotAI* ai = GET_PLAYERBOT_AI(player);
    if (!ai)
    {
        return;
    }

    // Snapshot the pending event under the lock; the map thread may already
    // have queued the next one.
    LlmBotEvent event;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        event = m_pendingEvent;
    }

    // The model wants to look something up before it answers. This is the
    // world thread, so the tools may touch the Player safely - the worker
    // thread that made the HTTP call must never do this.
    if (response.ok && response.needsTools)
    {
        if (request.toolRound >= kMaxToolRounds)
        {
            LOG_DEBUG("llmbots.brain", "{}: tool round limit reached, answering without tools", m_name);
            FallbackLine(player, event);
            return;
        }

        // Only the configured commander brain may receive an issuer for
        // action tools. The authorized sender GUID was captured when the
        // whisper entered the event bus, so character names are never used
        // for command authorization or issuer lookup.
        Player* issuer = nullptr;
        if (EqualsIgnoreCase(m_name, m_commanderName) &&
            event.kind == EventKind::Whisper &&
            !event.actor.IsEmpty())
        {
            issuer = ObjectAccessor::FindPlayer(event.actor);
        }

        std::vector<ToolResult> results;
        results.reserve(response.toolCalls.size());
        for (auto const& call : response.toolCalls)
        {
            ToolResult result;
            result.id = call.id;
            result.output = ExecuteTool(player, call, m_chatLog, issuer);
            LOG_DEBUG("llmbots.brain", "{}: tool {}({}) -> {}", m_name, call.name, call.arguments, result.output);
            results.push_back(std::move(result));
        }

        if (!m_client->Resume(request, response, std::move(results), nullptr))
        {
            LOG_DEBUG("llmbots.brain", "{}: tool resume refused", m_name);
            FallbackLine(player, event);
            return;
        }

        m_inFlight.store(true); // still mid-conversation
        return;
    }

    if (!response.ok)
    {
        if (request.purpose == Purpose::Social)
        {
            FallbackLine(player, event);
        }
        return;
    }

    if (request.schemaName == "bot_agenda")
    {
        ApplyAgenda(player, response);
    }
    else
    {
        ApplyUtterance(player, response, event);
    }
}

void LlmBotBrain::ApplyUtterance(Player* player, LlmResponse const& response, LlmBotEvent const& event)
{
    try
    {
        json parsed = json::parse(response.content);
        std::string line = parsed.value("line", "");
        std::string emote = parsed.contains("emote") && !parsed["emote"].is_null() ? parsed["emote"].get<std::string>() : "";
        std::string channel = parsed.value("channel", "say");

        ClampLine(line);
        if (line.empty())
        {
            FallbackLine(player, event);
            return;
        }

        if (uint32_t const emoteId = EmoteIdFor(emote); emoteId != 0)
        {
            player->HandleEmoteCommand(emoteId);
        }

        // The reply channel is a protocol fact, not a creative choice: an
        // answer to a whisper goes back as a whisper whatever the model put in
        // the schema field. Left to the model it almost always emits "say",
        // which makes the reply invisible to a whisperer out of say range.
        if (event.kind == EventKind::Whisper)
        {
            channel = "whisper";
        }

        PlayerbotAI* ai = GET_PLAYERBOT_AI(player);
        if (ai)
        {
            LOG_DEBUG("llmbots.brain", "{}: reply channel={} target='{}' line='{}'",
                m_name, channel, event.subject, line);

            if (channel == "yell")
            {
                ai->Yell(line);
            }
            else if (channel == "whisper" && event.kind == EventKind::Whisper)
            {
                ai->Whisper(line, event.subject);
            }
            else if (channel == "party")
            {
                ai->SayToParty(line);
            }
            else if (channel == "guild")
            {
                ai->SayToGuild(line);
            }
            else
            {
                ai->Say(line);
            }
        }

        // Remember what we said, so get_chat_history shows both sides of the
        // conversation rather than only what was said to us.
        if (m_chatLog)
        {
            ChatLine logLine;
            logLine.speaker = m_name;
            logLine.text = line;
            logLine.channel = channel;
            logLine.fromBot = true;
            logLine.gameTimeSec = m_bus ? m_bus->NowGameTime() : 0;
            m_chatLog->Push(m_guid, std::move(logLine));
        }

        // Bot-to-bot: register the turn in the conversation guard.
        if (event.kind == EventKind::BotSayNear && m_conversations)
        {
            m_conversations->RegisterTurn(m_guid, event.actor);
        }

        {
            std::lock_guard<std::mutex> guard(m_mutex);
            ++m_stats.replies;
        }
    }
    catch (json::exception const&)
    {
        FallbackLine(player, event);
    }
}

void LlmBotBrain::ApplyAgenda(Player* player, LlmResponse const& response)
{
    try
    {
        json parsed = json::parse(response.content);
        std::string const goal = parsed.value("goal", "idle");
        std::string const zone = parsed.contains("zone") && !parsed["zone"].is_null() ? parsed["zone"].get<std::string>() : "";
        std::string const stance = parsed.value("social_stance", "aloof");
        std::string const narrative = parsed.value("narrative", "");
        std::string const reason = parsed.value("reason", "");

        {
            std::lock_guard<std::mutex> guard(m_mutex);
            m_goal = goal + (zone.empty() ? "" : " (" + zone + ")");
            if (!narrative.empty())
            {
                m_goal += " - " + narrative;
            }
        }

        // Strategy toggles from the closed enum, applied through the
        // existing playerbots API. Unknown values were rejected by schema
        // validation upstream, so no whitelist check is needed here.
        if (parsed.contains("strategy_toggles") && parsed["strategy_toggles"].is_array())
        {
            PlayerbotAI* ai = GET_PLAYERBOT_AI(player);
            if (ai)
            {
                for (auto const& toggle : parsed["strategy_toggles"])
                {
                    if (toggle.is_string())
                    {
                        ai->ChangeStrategy(toggle.get<std::string>(), BOT_STATE_NON_COMBAT);
                    }
                }
            }
        }

        // Narrative is also said aloud - the bots narrate their own plans.
        PlayerbotAI* ai = GET_PLAYERBOT_AI(player);
        if (ai && !narrative.empty())
        {
            std::string line = narrative;
            ClampLine(line);
            ai->Say(line);
        }

        LOG_DEBUG("llmbots.brain", "{} agenda: goal={} stance={} reason={}", m_name, goal, stance, reason);
    }
    catch (json::exception const&)
    {
        // A malformed agenda is dropped entirely - the bot just keeps its
        // previous plan.
    }
}

void LlmBotBrain::FallbackLine(Player* player, LlmBotEvent const& event)
{
    size_t const idx = static_cast<size_t>(event.kind);
    std::string line = idx < kFallbackLines.size() ? kFallbackLines[idx] : "Hm.";

    PlayerbotAI* ai = GET_PLAYERBOT_AI(player);
    if (!ai)
    {
        return;
    }
    if (event.kind == EventKind::Whisper)
    {
        ai->Whisper(line, event.subject);
    }
    else
    {
        ai->Say(line);
    }
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        ++m_stats.fallenBack;
    }
}

} // namespace LlmBots
