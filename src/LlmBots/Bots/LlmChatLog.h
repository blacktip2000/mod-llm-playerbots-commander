/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 *
 * Recent chat, per bot. The event bus only carries events a bot has not acted
 * on yet, so it cannot answer "what did we talk about a minute ago" - events
 * are consumed. This keeps a short rolling transcript instead, which the
 * get_chat_history tool reads.
 */

#ifndef LLM_CHAT_LOG_H
#define LLM_CHAT_LOG_H

#include "ObjectGuid.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace LlmBots
{

struct ChatLine
{
    std::string speaker;    // character name
    std::string text;
    std::string channel;    // "whisper", "say", "yell"
    bool        fromBot = false; // true when this bot said it
    uint32_t    gameTimeSec = 0;
};

class LlmChatLog
{
public:
    // Record a line against one bot's history. Safe from any thread.
    void Push(ObjectGuid botGuid, ChatLine line);

    // Most recent lines first, capped at `limit`. Safe from any thread.
    std::vector<ChatLine> Recent(ObjectGuid botGuid, uint32_t limit) const;

    // Drop a bot's history (logout, roster removal).
    void Forget(ObjectGuid botGuid);

private:
    mutable std::mutex m_mutex;
    std::unordered_map<ObjectGuid, std::deque<ChatLine>> m_perBot; // newest first
    uint32_t const m_maxPerBot = 40;
};

} // namespace LlmBots

#endif // LLM_CHAT_LOG_H
