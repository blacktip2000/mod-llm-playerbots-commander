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

#include "LlmConversation.h"
#include <algorithm>

namespace LlmBots
{

void LlmConversationManager::Configure(uint32_t maxTurns, uint32_t maxConcurrent, uint32_t pairCooldownSec)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_maxTurns = maxTurns;
    m_maxConcurrent = maxConcurrent;
    m_pairCooldownSec = pairCooldownSec;
    m_configured = true;
}

LlmConversationManager::Conversation* LlmConversationManager::Find(ObjectGuid a, ObjectGuid b)
{
    for (auto& conv : m_conversations)
    {
        if ((conv.a == a && conv.b == b) || (conv.a == b && conv.b == a))
        {
            return &conv;
        }
    }
    return nullptr;
}

bool LlmConversationManager::MayReply(ObjectGuid speaker, ObjectGuid heard)
{
    if (!m_configured || m_maxTurns == 0)
    {
        return false;
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    auto const now = std::chrono::steady_clock::now();

    // Cooldown for this exact pair?
    std::pair<ObjectGuid, ObjectGuid> const key = std::minmax(speaker, heard);
    auto const cd = m_cooldowns.find(key);
    if (cd != m_cooldowns.end() && now < cd->second)
    {
        return false;
    }

    Conversation* conv = Find(speaker, heard);
    if (conv)
    {
        return conv->turns < m_maxTurns;
    }

    // New conversation: global cap.
    if (m_conversations.size() >= m_maxConcurrent)
    {
        return false;
    }
    return true;
}

bool LlmConversationManager::RegisterTurn(ObjectGuid speaker, ObjectGuid heard)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    auto const now = std::chrono::steady_clock::now();

    Conversation* conv = Find(speaker, heard);
    if (!conv)
    {
        if (m_conversations.size() >= m_maxConcurrent)
        {
            return false;
        }
        m_conversations.push_back(Conversation{ speaker, heard, 0, now });
        conv = &m_conversations.back();
    }

    ++conv->turns;
    if (conv->turns >= m_maxTurns)
    {
        // Conversation over: pair goes on cooldown, bookkeeping removed.
        m_cooldowns[std::minmax(speaker, heard)] = now + std::chrono::seconds(m_pairCooldownSec);
        m_conversations.erase(
            std::remove_if(m_conversations.begin(), m_conversations.end(),
                [&](Conversation const& c) { return &c == conv; }),
            m_conversations.end());
        return false;
    }
    return true;
}

bool LlmConversationManager::ConversationActive(ObjectGuid a, ObjectGuid b) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    for (auto const& conv : m_conversations)
    {
        if ((conv.a == a && conv.b == b) || (conv.a == b && conv.b == a))
        {
            return true;
        }
    }
    return false;
}

void LlmConversationManager::Reset()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_conversations.clear();
    m_cooldowns.clear();
}

} // namespace LlmBots
