/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 */

#include "LlmEventBus.h"

#include "GameTime.h"

#include <algorithm>
#include <limits>

namespace LlmBots
{

uint32_t LlmEventBus::NowGameTime() const
{
    // GameTime::GetGameTime() is milliseconds since the process started.
    // Every consumer (cache TTLs, event windows) thinks in seconds.
    return static_cast<uint32_t>(GameTime::GetGameTime().count() / 1000u);
}

void LlmEventBus::Push(LlmBotEvent const& event)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    LlmBotEvent copy = event;
    if (copy.gameTimeSec == 0)
    {
        copy.gameTimeSec = NowGameTime();
    }

    m_perBot.push_front(copy);
    if (m_perBot.size() > m_maxPerBot)
    {
        m_perBot.pop_back();
    }

    m_global.push_front(copy);
    if (m_global.size() > m_maxGlobal)
    {
        m_global.pop_back();
    }
}

void LlmEventBus::DrainFor(ObjectGuid guid, std::vector<LlmBotEvent>& out)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    for (auto itr = m_perBot.begin(); itr != m_perBot.end();)
    {
        if (itr->guid == guid)
        {
            out.push_back(*itr);
            itr = m_perBot.erase(itr);
        }
        else
        {
            ++itr;
        }
    }
}

uint32_t SocialPriority(EventKind kind)
{
    switch (kind)
    {
    case EventKind::Whisper:
    case EventKind::DuelRequest:
        return 0;
    case EventKind::SayNear:
        return 1;
    case EventKind::BotSayNear:
        return 3;
    default:
        return 2; // the bot's own life events - died, levelled, looted, zoned
    }
}

bool LlmEventBus::TakeBestFor(ObjectGuid guid, LlmBotEvent& out, uint32_t maxPriority)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    // The deque is newest first, so the first event in the winning priority
    // band is also the freshest one - answer the latest whisper, not a stale
    // one from a minute ago.
    auto best = m_perBot.end();
    uint32_t bestPriority = std::numeric_limits<uint32_t>::max();
    for (auto itr = m_perBot.begin(); itr != m_perBot.end(); ++itr)
    {
        if (itr->guid != guid)
        {
            continue;
        }
        uint32_t const priority = SocialPriority(itr->kind);
        if (priority > maxPriority)
        {
            continue;
        }
        if (priority < bestPriority)
        {
            bestPriority = priority;
            best = itr;
            if (priority == 0)
            {
                break; // nothing outranks a whisper
            }
        }
    }

    if (best == m_perBot.end())
    {
        return false;
    }

    out = *best;
    m_perBot.erase(best);
    return true;
}

void LlmEventBus::DrainGlobal(std::vector<LlmBotEvent>& out, uint32_t sinceGameTimeSec)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    for (auto const& event : m_global)
    {
        if (event.gameTimeSec >= sinceGameTimeSec)
        {
            out.push_back(event);
        }
    }
}

} // namespace LlmBots
