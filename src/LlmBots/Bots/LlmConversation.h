/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 *
 * Bot-to-bot banter guard. Two LLM bots replying to each other is the one
 * failure mode where every individual call looks justified and the aggregate
 * is unbounded, so exchanges are first-class objects:
 *
 *   - hard cap on total turns (default 4),
 *   - a shared token budget for the whole conversation,
 *   - a cooldown before the same pair may start another,
 *   - a global cap on concurrent conversations.
 *
 * All members are called from the map thread(s) of the bots involved; the
 * guard is mutex-protected regardless.
 */

#ifndef LLM_CONVERSATION_H
#define LLM_CONVERSATION_H

#include "ObjectGuid.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace LlmBots
{

class LlmConversationManager
{
public:
    // Configures limits. Call once at startup.
    void Configure(uint32_t maxTurns, uint32_t maxConcurrent, uint32_t pairCooldownSec);

    // True if a reply from `speaker` to `heard` may be started/continued.
    // Consumes nothing; every accepted reply must call RegisterTurn().
    bool MayReply(ObjectGuid speaker, ObjectGuid heard);

    // Registers one turn in the conversation; may end it (returns false).
    bool RegisterTurn(ObjectGuid speaker, ObjectGuid heard);

    // True while a conversation between the pair is live.
    bool ConversationActive(ObjectGuid a, ObjectGuid b) const;

    void Reset();

private:
    struct Conversation
    {
        ObjectGuid a;
        ObjectGuid b;
        uint32_t turns = 0;
        std::chrono::steady_clock::time_point started;
    };

    Conversation* Find(ObjectGuid a, ObjectGuid b);

    mutable std::mutex m_mutex;
    std::vector<Conversation> m_conversations;
    std::map<std::pair<ObjectGuid, ObjectGuid>, std::chrono::steady_clock::time_point> m_cooldowns;

    uint32_t m_maxTurns = 4;
    uint32_t m_maxConcurrent = 3;
    uint32_t m_pairCooldownSec = 60;
    bool m_configured = false;
};

} // namespace LlmBots

#endif // LLM_CONVERSATION_H
