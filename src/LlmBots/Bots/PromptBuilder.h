/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 *
 * Pure string assembly - no WoW or playerbots types. Takes persona + context
 * snapshot structs and returns the system/user prompt halves.
 */

#ifndef PROMPT_BUILDER_H
#define PROMPT_BUILDER_H

#include "LlmEventBus.h"
#include "LlmTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace LlmBots
{

struct Persona;

// Snapshot of a bot's situation at prompt-build time. Plain strings only.
struct BotContext
{
    std::string name;        // character name
    std::string className;   // e.g. "Priest"
    uint8_t     level = 1;
    std::string zone;        // localized zone name
    std::string goal;        // current Tier 2 goal narrative
    std::string healthFrac;  // "full", "hurt", "critical"
    std::vector<std::string> memories; // durable reflections
};

// System prompt: persona, rules of engagement (schema, one line, in-fiction).
std::string BuildSystemPrompt(Persona const& persona, BotContext const& context, ModelTier tier);

// User prompt for a Tier 1 social event.
std::string BuildSocialPrompt(LlmBotEvent const& event, BotContext const& context);

// User prompt for a Tier 2 planning call.
std::string BuildPlanPrompt(BotContext const& context);

// User prompt for the chronicler digest. `lines` are pre-rendered
// "<name> <event>" strings (the chronicler resolves names itself).
std::string BuildChroniclerPrompt(std::vector<std::string> const& lines, std::string const& asker,
    std::string const& question);

} // namespace LlmBots

#endif // PROMPT_BUILDER_H
