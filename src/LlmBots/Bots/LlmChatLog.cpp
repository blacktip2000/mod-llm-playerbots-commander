/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 */

#include "LlmChatLog.h"

namespace LlmBots
{

void LlmChatLog::Push(ObjectGuid botGuid, ChatLine line)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    auto& lines = m_perBot[botGuid];
    lines.push_front(std::move(line));
    while (lines.size() > m_maxPerBot)
    {
        lines.pop_back();
    }
}

std::vector<ChatLine> LlmChatLog::Recent(ObjectGuid botGuid, uint32_t limit) const
{
    std::lock_guard<std::mutex> guard(m_mutex);

    std::vector<ChatLine> out;
    auto const itr = m_perBot.find(botGuid);
    if (itr == m_perBot.end())
    {
        return out;
    }
    for (auto const& line : itr->second)
    {
        if (out.size() >= limit)
        {
            break;
        }
        out.push_back(line);
    }
    return out;
}

void LlmChatLog::Forget(ObjectGuid botGuid)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_perBot.erase(botGuid);
}

} // namespace LlmBots
