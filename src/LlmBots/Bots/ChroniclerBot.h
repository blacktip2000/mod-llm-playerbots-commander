/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 *
 * The Chronicler - an observer bot type. It never acts; it watches every LLM
 * bot's activity on the event bus and answers whispers with a digest of what
 * the adventurers have been up to.
 *
 * Cost discipline:
 *   - a cached digest, refreshed at most once per DigestSec AND only when the
 *     event window has materially changed; repeated whispers are served from
 *     cache with zero LLM calls,
 *   - a per-whisperer rate limit, so one player spamming it cannot spend the
 *     budget.
 *
 * Runs on the world thread.
 */

#ifndef CHRONICLER_BOT_H
#define CHRONICLER_BOT_H

#include "LlmTypes.h"

#include "ObjectGuid.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

class Player;

namespace LlmBots
{

class LlmClient;
class LlmEventBus;
class LlmBotRegistry;

class ChroniclerBot
{
public:
    ChroniclerBot(std::string name, ObjectGuid guid);

    void Configure(LlmClient* client, LlmEventBus* bus, LlmBotRegistry* registry, uint32_t digestSec);

    // Called from the world thread when someone whispers the chronicler.
    void OnWhisper(Player* whisperer, std::string const& msg);

    // Applies a finished digest request (world thread).
    void ApplyCompletion(LlmResponse const& response);

    bool Enabled() const;
    ObjectGuid GetGuid() const { return m_guid; }

private:
    void SubmitDigest(Player* whisperer, std::string const& question);
    void Reply(Player* whisperer, std::string const& text);

    std::string m_name;
    ObjectGuid m_guid;

    LlmClient* m_client = nullptr;
    LlmEventBus* m_bus = nullptr;
    LlmBotRegistry* m_registry = nullptr;
    uint32_t m_digestSec = 60;

    mutable std::mutex m_mutex;
    std::chrono::steady_clock::time_point m_lastDigestAt;
    uint32_t m_lastWindowGameTime = 0;
    std::atomic<bool> m_inFlight = false;

    // Cached digest.
    std::string m_cachedHeadline;
    std::string m_cachedDigest;
    uint32_t m_cachedAt = 0;

    // Per-whisperer rate limit (10 s floor per asker).
    std::map<std::string, std::chrono::steady_clock::time_point> m_lastWhisperBy;

    // Whom the in-flight digest answers.
    ObjectGuid m_pendingAsker;
    std::string m_pendingAskerName;
};

} // namespace LlmBots

#endif // CHRONICLER_BOT_H
