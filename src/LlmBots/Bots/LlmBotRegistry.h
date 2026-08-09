/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 *
 * The roster - which characters are LLM bots, their persona and type - and the
 * persona table. Loaded from the playerbots database, hot-reloadable without a
 * server restart (.llmbot reload).
 *
 * Roster bots live on a dedicated, NON-random account (LlmBots.BotAccount).
 * RandomPlayerbotMgr rotation is account-gated, so a roster bot on such an
 * account never enters currentBots, is never logged out by the rotation, and
 * is pinned online for as long as the server runs.
 *
 * Reads are plain structs; the registry itself is guarded so the map thread
 * can look up by guid/name without blocking the world thread's reload.
 */

#ifndef LLM_BOT_REGISTRY_H
#define LLM_BOT_REGISTRY_H

#include "DatabaseEnvFwd.h"
#include "ObjectGuid.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace LlmBots
{

enum class BotType : uint8_t
{
    Actor = 0,     // talks, plans, acts
    Chronicler = 1 // observes all LLM bots, answers whispers with a digest
};

struct Persona
{
    std::string key;
    std::string displayName;
    std::string brief;         // who they are
    std::string voice;         // how they talk
    std::string quirks;        // habits, gags, delusions
    uint8_t     selfAwareness = 0; // 0..3: 0 fully in-fiction, 3 "the loop"
};

struct RosterEntry
{
    ObjectGuid  guid;
    std::string name;
    std::string personaKey;
    BotType     botType = BotType::Actor;
    bool        enabled = true;
};

class LlmBotRegistry
{
public:
    void Clear();

    // Parses the two query results and applies the snapshot (world thread).
    // Queries are issued by LlmBotsModule; the results are handed back here.
    void ApplyFromQueries(QueryResult rosterResult, QueryResult personaResult);

    // Snapshot of all entries without locking (world thread, already guarded).
    void ApplyRoster(std::vector<RosterEntry> roster, std::vector<Persona> personas);

    RosterEntry const* FindByGuid(ObjectGuid guid) const;
    RosterEntry const* FindByName(std::string const& name) const;
    Persona const* FindPersona(std::string const& key) const;

    // All entries (actors only by default). Safe to call from map thread.
    std::vector<RosterEntry> Entries(bool actorsOnly = true) const;

    uint32_t Count() const;

private:
    mutable std::mutex m_mutex;
    std::map<ObjectGuid, RosterEntry> m_roster;
    std::map<std::string, Persona> m_personas;
};

} // namespace LlmBots

#endif // LLM_BOT_REGISTRY_H
