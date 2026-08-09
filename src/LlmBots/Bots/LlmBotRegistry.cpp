/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 */

#include "LlmBotRegistry.h"

#include "CharacterCache.h"
#include "DatabaseEnv.h"
#include "Log.h"

#include <algorithm>

namespace LlmBots
{

void LlmBotRegistry::Clear()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_roster.clear();
    m_personas.clear();
}

void LlmBotRegistry::ApplyFromQueries(QueryResult rosterResult, QueryResult personaResult)
{
    std::vector<RosterEntry> roster;
    std::vector<Persona> personas;

    if (rosterResult)
    {
        do
        {
            Field* fields = rosterResult->Fetch();

            RosterEntry entry;
            entry.name = fields[0].Get<std::string>();
            entry.personaKey = fields[1].Get<std::string>();
            entry.botType = static_cast<BotType>(fields[2].Get<uint8>());
            entry.enabled = fields[3].Get<bool>();
            entry.guid = sCharacterCache->GetCharacterGuidByName(entry.name);
            if (entry.guid.IsEmpty())
            {
                LOG_WARN("llmbots.registry", "Roster entry '{}' has no character - skipped (create the character first)", entry.name);
                continue;
            }
            roster.push_back(std::move(entry));
        } while (rosterResult->NextRow());
    }

    if (personaResult)
    {
        do
        {
            Field* fields = personaResult->Fetch();

            Persona persona;
            persona.key = fields[0].Get<std::string>();
            persona.displayName = fields[1].Get<std::string>();
            persona.brief = fields[2].Get<std::string>();
            persona.voice = fields[3].Get<std::string>();
            persona.quirks = fields[4].Get<std::string>();
            persona.selfAwareness = fields[5].Get<uint8>();
            personas.push_back(std::move(persona));
        } while (personaResult->NextRow());
    }

    ApplyRoster(std::move(roster), std::move(personas));
}

void LlmBotRegistry::ApplyRoster(std::vector<RosterEntry> roster, std::vector<Persona> personas)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_roster.clear();
    m_personas.clear();
    for (auto& entry : roster)
    {
        if (!entry.guid.IsEmpty())
        {
            m_roster.emplace(entry.guid, entry);
        }
    }
    for (auto& persona : personas)
    {
        m_personas.emplace(persona.key, persona);
    }
    LOG_INFO("llmbots.registry", "Loaded roster: {} bots, {} personas", m_roster.size(), m_personas.size());
}

RosterEntry const* LlmBotRegistry::FindByGuid(ObjectGuid guid) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    auto const itr = m_roster.find(guid);
    return itr == m_roster.end() ? nullptr : &itr->second;
}

RosterEntry const* LlmBotRegistry::FindByName(std::string const& name) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    for (auto const& [guid, entry] : m_roster)
    {
        if (entry.name == name)
        {
            return &entry;
        }
    }
    return nullptr;
}

Persona const* LlmBotRegistry::FindPersona(std::string const& key) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    auto const itr = m_personas.find(key);
    return itr == m_personas.end() ? nullptr : &itr->second;
}

std::vector<RosterEntry> LlmBotRegistry::Entries(bool actorsOnly) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::vector<RosterEntry> out;
    for (auto const& [guid, entry] : m_roster)
    {
        if (!entry.enabled)
        {
            continue;
        }
        if (actorsOnly && entry.botType != BotType::Actor)
        {
            continue;
        }
        out.push_back(entry);
    }
    return out;
}

uint32_t LlmBotRegistry::Count() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return static_cast<uint32_t>(m_roster.size());
}

} // namespace LlmBots
