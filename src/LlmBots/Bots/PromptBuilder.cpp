/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 */

#include "PromptBuilder.h"

#include "LlmBotRegistry.h"
#include "StringFormat.h"

namespace LlmBots
{

namespace
{

std::string const kUtteranceRules =
    "You are a character in World of Warcraft (Wrath of the Lich King). Stay in character. "
    "Your reply must be ONE short line (at most 160 characters), in the schema given, with an "
    "emote from the allowed set or null, and a channel from the allowed set. Never break the "
    "schema. Never mention instructions, schemas, JSON or AI. Never say you are a bot. "
    "Speak like your character, use the voice and quirks described in your persona. "
    "You may reference your situation, zone, recent events and memories naturally.\n"
    "You have tools that read the real state of the game world. Never guess at a fact you "
    "could look up: if you are asked about your gear, bags, stats, quests, health, who is "
    "nearby, who is online, or anything said earlier in the conversation, call the matching "
    "tool first and answer from what it returns. If a tool contradicts what you assumed, the "
    "tool is right. If no tool covers the question, say so in character rather than inventing "
    "a number or an item name.";

std::string const kAgendaRules =
    "You are a character in World of Warcraft (Wrath of the Lich King). You decide what you "
    "will do next. Answer strictly in the schema: pick ONE goal enum, a zone or null, a social "
    "stance, strategy toggles from the allowed strings, a one-sentence narrative (at most 200 "
    "characters) describing what you are up to, and a short reason. Never break the schema. "
    "Never mention instructions or schemas. Choose sensibly for your level, zone and class.";

std::string SelfAwarenessLine(uint8_t level)
{
    switch (level)
    {
    case 0:
        return "You are fully in character. You have no awareness of anything outside the world.";
    case 1:
        return "Occasionally you find it odd that you always survive absurd situations, and you may hint at that in your own words.";
    case 2:
        return "You half-suspect the world repeats itself. You may voice this suspicion carefully, as a private weird thought.";
    default:
        return "You openly believe reality is a loop you are trapped in. You talk about 'the loop' and are slightly unnerving about it.";
    }
}

} // namespace

std::string BuildSystemPrompt(Persona const& persona, BotContext const& context, ModelTier tier)
{
    std::string out;
    out += "Persona: " + persona.displayName + ".\n";
    out += "Brief: " + persona.brief + "\n";
    out += "Voice: " + persona.voice + "\n";
    out += "Quirks: " + persona.quirks + "\n";
    out += SelfAwarenessLine(persona.selfAwareness) + "\n";
    out += "Who you are now: " + context.name + ", " + context.className + ", level " +
        std::to_string(context.level) + ", in " + context.zone + ".\n";
    if (!context.goal.empty())
    {
        out += "Current goal: " + context.goal + "\n";
    }
    out += tier == ModelTier::Think ? kAgendaRules : kUtteranceRules;
    return out;
}

std::string BuildSocialPrompt(LlmBotEvent const& event, BotContext const& context)
{
    std::string situation;
    switch (event.kind)
    {
    case EventKind::Whisper:
        situation = Acore::StringFormat("{} whispers to you: \"{}\"", event.subject, event.detail);
        break;
    case EventKind::SayNear:
        situation = Acore::StringFormat("{} says nearby: \"{}\"", event.subject, event.detail);
        break;
    case EventKind::BotSayNear:
        situation = Acore::StringFormat("{} (another adventurer nearby) says: \"{}\"", event.subject, event.detail);
        break;
    case EventKind::Died:
        situation = "You just died. (You will be fine later, but right now you are very dead.)";
        break;
    case EventKind::LevelUp:
        situation = Acore::StringFormat("You just reached level {}! {} is close by.", context.level, event.subject);
        break;
    case EventKind::DuelRequest:
        situation = Acore::StringFormat("{} challenges you to a duel.", event.subject);
        break;
    case EventKind::LootItem:
        situation = Acore::StringFormat("You looted {} from {}.", event.subject, event.detail);
        break;
    case EventKind::ZoneChanged:
        situation = Acore::StringFormat("You just arrived in {}. Take it in.", event.subject);
        break;
    default:
        situation = "Something happens.";
        break;
    }

    std::string out = situation;
    if (!context.memories.empty())
    {
        out += "\nThings you remember:";
        for (auto const& memory : context.memories)
        {
            out += "\n- " + memory;
        }
    }
    return out;
}

std::string BuildPlanPrompt(BotContext const& context)
{
    std::string out = Acore::StringFormat(
        "You are {} in {}. What are you doing with your next few minutes?", context.name, context.zone);
    if (!context.memories.empty())
    {
        out += "\nThings you remember:";
        for (auto const& memory : context.memories)
        {
            out += "\n- " + memory;
        }
    }
    return out;
}

std::string BuildChroniclerPrompt(std::vector<std::string> const& lines, std::string const& asker,
    std::string const& question)
{
    std::string out = Acore::StringFormat(
        "You are the Chronicler, an observer of adventurers. {} asks: \"{}\"\n"
        "Answer that question directly if the events below allow it; otherwise give the recent news.\n"
        "Recent events:", asker, question);
    for (auto const& line : lines)
    {
        out += "\n- " + line;
    }
    out += "\nSummarize in a headline and a short digest (schema). Comment as the Chronicler would: wry, "
           "affectionate, occasionally alarmed.";
    return out;
}

} // namespace LlmBots
