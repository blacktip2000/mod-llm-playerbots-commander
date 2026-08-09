/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 *
 * In-process event bus. Game hooks push LlmBotEvent structs from the map or
 * world thread; bot brains and the chronicler consume them.
 *
 * Threading: push and drain are both mutex-guarded. Event payloads are plain
 * strings + a timestamp - no game pointers ever travel on the bus.
 */

#ifndef LLM_EVENT_BUS_H
#define LLM_EVENT_BUS_H

#include "ObjectGuid.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

namespace LlmBots
{

// What happened. Kind selects the prompt template in PromptBuilder.
enum class EventKind : uint8_t
{
    Whisper,       // a player whispered this bot
    SayNear,       // a player said something near this bot
    BotSayNear,    // another LLM bot said something near this bot
    Died,          // this bot died
    LevelUp,       // this bot levelled up
    DuelRequest,   // a player challenged this bot to a duel
    LootItem,      // this bot looted something
    ZoneChanged,   // this bot entered a notable zone
    GoalCompleted  // this bot finished its current goal
};

struct LlmBotEvent
{
    EventKind       kind = EventKind::SayNear;
    ObjectGuid      guid;                 // the bot the event is about
    ObjectGuid      actor;                // who caused it, when it is another bot
    std::string     subject;              // e.g. whisperer name, zone name
    std::string     detail;               // e.g. the message text
    uint32_t        gameTimeSec = 0;
};

// Lower sorts first. A human waiting on a whisper outranks anything a bot
// said, and a player speaking nearby outranks bot-to-bot banter.
uint32_t SocialPriority(EventKind kind);

class LlmEventBus
{
public:
    LlmEventBus() = default;

    // Push an event. Safe from any thread.
    void Push(LlmBotEvent const& event);

    // Move all events for one bot out. Safe from any thread.
    void DrainFor(ObjectGuid guid, std::vector<LlmBotEvent>& out);

    // Take the single most important pending event for one bot, newest first
    // within a priority band, and leave the rest queued. Events whose
    // SocialPriority is above maxPriority are ignored and left alone, so a
    // caller in cooldown can ask for player-directed events only. Returns
    // false when the bot has nothing waiting. Safe from any thread.
    //
    // Prefer this over DrainFor: a bot can only answer one event per call, so
    // draining the whole queue throws away everything it could not act on -
    // including a whisper that landed while the bot was busy.
    bool TakeBestFor(ObjectGuid guid, LlmBotEvent& out,
        uint32_t maxPriority = std::numeric_limits<uint32_t>::max());

    // Snapshot of the recent global window (for the chronicler). Safe.
    void DrainGlobal(std::vector<LlmBotEvent>& out, uint32_t sinceGameTimeSec);

    uint32_t NowGameTime() const;

private:
    mutable std::mutex m_mutex;
    std::deque<LlmBotEvent> m_perBot;      // ring per bot: newest first
    std::deque<LlmBotEvent> m_global;      // ring, newest first
    uint32_t const m_maxPerBot = 24;
    uint32_t const m_maxGlobal = 512;
};

} // namespace LlmBots

#endif // LLM_EVENT_BUS_H
