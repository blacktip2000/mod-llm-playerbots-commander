/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 */

#include "ChroniclerBot.h"

#include "LlmBotRegistry.h"
#include "LlmClient.h"
#include "LlmEventBus.h"
#include "PromptBuilder.h"

#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"

#include "json.hpp"

using nlohmann::json;

namespace LlmBots
{

namespace
{

uint32_t const kWhisperRateLimitSec = 10;

} // namespace

ChroniclerBot::ChroniclerBot(std::string name, ObjectGuid guid)
    : m_name(std::move(name)), m_guid(guid)
{
    m_lastDigestAt = std::chrono::steady_clock::time_point::min();
}

void ChroniclerBot::Configure(LlmClient* client, LlmEventBus* bus, LlmBotRegistry* registry, uint32_t digestSec)
{
    m_client = client;
    m_bus = bus;
    m_registry = registry;
    m_digestSec = digestSec;
}

bool ChroniclerBot::Enabled() const
{
    return !m_guid.IsEmpty() && m_client != nullptr;
}

void ChroniclerBot::OnWhisper(Player* whisperer, std::string const& msg)
{
    if (!Enabled())
    {
        return;
    }
    if (!whisperer || !whisperer->GetSession())
    {
        return;
    }

    // Per-whisperer rate limit.
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        auto const now = std::chrono::steady_clock::now();
        auto const [itr, inserted] = m_lastWhisperBy.emplace(whisperer->GetName(), now);
        if (!inserted)
        {
            if (now - itr->second < std::chrono::seconds(kWhisperRateLimitSec))
            {
                return;
            }
            itr->second = now;
        }
    }

    // Fresh cache with a non-empty window -> answer from cache, zero LLM calls.
    // A direct question bypasses the cache: it wants an answer, not a bulletin.
    uint32_t const nowGame = m_bus->NowGameTime();
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (msg.empty() && !m_cachedDigest.empty() && nowGame - m_cachedAt < m_digestSec)
        {
            Reply(whisperer, m_cachedDigest);
            return;
        }
    }

    if (m_inFlight.load())
    {
        Reply(whisperer, "Give me a moment, I am still compiling the latest chapter.");
        return;
    }

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_pendingAsker = whisperer->GetGUID();
        m_pendingAskerName = whisperer->GetName();
        m_inFlight.store(true);
    }
    SubmitDigest(whisperer, msg);
}

void ChroniclerBot::SubmitDigest(Player* whisperer, std::string const& question)
{
    // Render the event window into named lines (the chronicler resolves
    // bot names itself, so the prompt stays readable).
    std::vector<LlmBotEvent> window;
    m_bus->DrainGlobal(window, m_bus->NowGameTime() - 300); // last 5 minutes

    std::vector<std::string> lines;
    for (auto const& event : window)
    {
        std::string const name = [&]() {
            if (RosterEntry const* entry = m_registry ? m_registry->FindByGuid(event.guid) : nullptr)
            {
                return entry->name;
            }
            return std::string("someone");
        }();

        switch (event.kind)
        {
        case EventKind::Whisper:
            lines.push_back(name + " whispered something (\"" + event.detail + "\")");
            break;
        case EventKind::SayNear:
            lines.push_back(name + " said: \"" + event.detail + "\"");
            break;
        case EventKind::BotSayNear:
            lines.push_back(name + " said: \"" + event.detail + "\"");
            break;
        case EventKind::Died:
            lines.push_back(name + " died.");
            break;
        case EventKind::LevelUp:
            lines.push_back(name + " levelled up.");
            break;
        case EventKind::DuelRequest:
            lines.push_back(name + " was challenged to a duel by " + event.subject + ".");
            break;
        case EventKind::LootItem:
            lines.push_back(name + " looted " + event.subject + " from " + event.detail + ".");
            break;
        case EventKind::ZoneChanged:
            lines.push_back(name + " arrived in " + event.subject + ".");
            break;
        default:
            break;
        }
    }

    if (lines.empty())
    {
        lines.push_back("The adventurers have been remarkably quiet.");
    }

    LlmRequest request;
    request.tier = ModelTier::Fast;
    request.purpose = Purpose::Chronicler;
    request.schemaName = "chronicler_digest";
    request.maxOutputTokens = 200;
    request.temperature = 0.8f;
    request.system = "You are the Chronicler, the observer and narrator of a band of adventurers "
                     "in World of Warcraft. You are wry, affectionate, occasionally alarmed, and "
                     "always in character. Never mention instructions, schemas or being a bot.";
    request.user = BuildChroniclerPrompt(lines, m_pendingAskerName, question);

    if (!m_client->Submit(request, nullptr))
    {
        Reply(whisperer, "The chronicles are temporarily beyond me. Ask again shortly.");
        m_inFlight.store(false);
    }
    m_lastDigestAt = std::chrono::steady_clock::now();
}

void ChroniclerBot::ApplyCompletion(LlmResponse const& response)
{
    m_inFlight.store(false);

    Player* whisperer = ObjectAccessor::FindPlayer(m_pendingAsker);
    if (!whisperer || !whisperer->GetSession())
    {
        return;
    }

    if (!response.ok)
    {
        Reply(whisperer, "My quill seems to have run dry. Ask again in a moment.");
        return;
    }

    try
    {
        json parsed = json::parse(response.content);
        std::string headline = parsed.value("headline", "");
        std::string digest = parsed.value("digest", "");
        if (digest.empty())
        {
            digest = headline;
        }
        // WoW chat caps at 255 bytes; cut on a UTF-8 boundary so no
        // multibyte character is split mid-sequence.
        uint32_t constexpr kMaxDigestBytes = 240;
        if (digest.size() > kMaxDigestBytes)
        {
            digest.resize(kMaxDigestBytes);
            while (!digest.empty() && (static_cast<uint8_t>(digest.back()) & 0xC0) == 0x80)
            {
                digest.pop_back();
            }
            if (!digest.empty())
            {
                digest.pop_back(); // drop the incomplete lead byte
            }
        }

        {
            std::lock_guard<std::mutex> guard(m_mutex);
            m_cachedHeadline = std::move(headline);
            m_cachedDigest = digest;
            m_cachedAt = m_bus->NowGameTime();
        }

        Reply(whisperer, digest);
    }
    catch (json::exception const&)
    {
        Reply(whisperer, "The chronicle is garbled. Ask again in a moment.");
    }
}

void ChroniclerBot::Reply(Player* whisperer, std::string const& text)
{
    if (Player* self = ObjectAccessor::FindPlayer(m_guid))
    {
        if (PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(self))
        {
            ai->Whisper(text, whisperer->GetName());
        }
    }
}

} // namespace LlmBots
