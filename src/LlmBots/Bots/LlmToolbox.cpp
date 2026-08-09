/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 */

#include "LlmToolbox.h"

#include "LlmChatLog.h"

#include "Bag.h"
#include "DBCStores.h"
#include "Group.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Playerbots.h"
#include "QuestDef.h"
#include "SharedDefines.h"
#include "World.h"

#include "json.hpp"

#include <algorithm>

using nlohmann::json;

namespace LlmBots
{

namespace
{

char const* kClassNames[MAX_CLASSES] = { "", "Warrior", "Paladin", "Hunter", "Rogue", "Priest",
    "Death Knight", "Shaman", "Mage", "Warlock", "", "Druid" };

char const* kRaceNames[] = { "", "Human", "Orc", "Dwarf", "Night Elf", "Undead", "Tauren", "Gnome",
    "Troll", "", "Blood Elf", "Draenei" };

// How far a bot can see other players for get_nearby_players.
float const kLookRangeYards = 60.0f;

char const* ClassName(uint8 cls)
{
    return cls < MAX_CLASSES ? kClassNames[cls] : "";
}

char const* RaceName(uint8 race)
{
    return race < sizeof(kRaceNames) / sizeof(kRaceNames[0]) ? kRaceNames[race] : "";
}

json ErrorObject(std::string const& message)
{
    return json::object({ { "error", message } });
}

// ---------------------------------------------------------------------------
// Tool implementations. Each returns a json value; the caller stringifies.
// ---------------------------------------------------------------------------

json ToolCharacterSheet(Player* bot)
{
    json out = json::object();
    out["name"] = bot->GetName();
    out["race"] = RaceName(bot->getRace());
    out["class"] = ClassName(bot->getClass());
    out["level"] = bot->GetLevel();
    out["health"] = bot->GetHealth();
    out["max_health"] = bot->GetMaxHealth();
    out["alive"] = bot->IsAlive();
    out["in_combat"] = bot->IsInCombat();
    out["money_copper"] = bot->GetMoney();

    if (bot->getPowerType() == POWER_MANA)
    {
        out["mana"] = bot->GetPower(POWER_MANA);
        out["max_mana"] = bot->GetMaxPower(POWER_MANA);
    }

    out["strength"] = static_cast<uint32>(bot->GetStat(STAT_STRENGTH));
    out["agility"] = static_cast<uint32>(bot->GetStat(STAT_AGILITY));
    out["stamina"] = static_cast<uint32>(bot->GetStat(STAT_STAMINA));
    out["intellect"] = static_cast<uint32>(bot->GetStat(STAT_INTELLECT));
    out["spirit"] = static_cast<uint32>(bot->GetStat(STAT_SPIRIT));

    if (AreaTableEntry const* zone = sAreaTableStore.LookupEntry(bot->GetZoneId()))
    {
        out["zone"] = zone->area_name[0];
    }
    return out;
}

json ToolEquippedItems(Player* bot)
{
    json items = json::array();
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
        {
            continue;
        }
        ItemTemplate const* proto = item->GetTemplate();
        if (!proto)
        {
            continue;
        }
        items.push_back(json::object({
            { "name", proto->Name1 },
            { "item_level", proto->ItemLevel },
            { "quality", proto->Quality },
            { "required_level", proto->RequiredLevel } }));
    }
    return json::object({ { "equipped", items } });
}

json ToolBagItems(Player* bot)
{
    json items = json::array();

    auto push = [&items](Item* item)
    {
        ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
        if (!proto)
        {
            return;
        }
        items.push_back(json::object({
            { "name", proto->Name1 },
            { "count", item->GetCount() },
            { "quality", proto->Quality } }));
    };

    // The backpack.
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
    {
        push(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
    }

    // Equipped bags.
    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
    {
        Bag* bag = bot->GetBagByPos(bagSlot);
        if (!bag)
        {
            continue;
        }
        for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
        {
            push(bot->GetItemByPos(bagSlot, static_cast<uint8>(slot)));
        }
    }

    return json::object({ { "bags", items } });
}

json ToolQuestLog(Player* bot)
{
    json quests = json::array();
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 const questId = bot->GetQuestSlotQuestId(slot);
        if (!questId)
        {
            continue;
        }
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
        {
            continue;
        }
        quests.push_back(json::object({
            { "title", quest->GetTitle() },
            { "level", quest->GetQuestLevel() },
            { "complete", bot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE } }));
    }
    return json::object({ { "quests", quests } });
}

json ToolNearbyPlayers(Player* bot)
{
    json players = json::array();
    Map* map = bot->GetMap();
    if (!map)
    {
        return json::object({ { "nearby", players } });
    }

    for (auto const& pair : ObjectAccessor::GetPlayers())
    {
        Player* other = pair.second;
        if (!other || other == bot || !other->IsInWorld())
        {
            continue;
        }
        if (other->GetMap() != map)
        {
            continue;
        }
        float const distance = bot->GetDistance2d(other);
        if (distance > kLookRangeYards)
        {
            continue;
        }
        players.push_back(json::object({
            { "name", other->GetName() },
            { "level", other->GetLevel() },
            { "class", ClassName(other->getClass()) },
            { "yards_away", static_cast<uint32>(distance) },
            { "alive", other->IsAlive() } }));
    }
    return json::object({ { "nearby", players } });
}

json ToolGroupMembers(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group)
    {
        return json::object({ { "in_group", false }, { "members", json::array() } });
    }

    json members = json::array();
    for (auto const& slot : group->GetMemberSlots())
    {
        members.push_back(json::object({
            { "name", slot.name },
            { "online", ObjectAccessor::FindPlayer(slot.guid) != nullptr } }));
    }
    return json::object({
        { "in_group", true },
        { "is_raid", group->isRaidGroup() },
        { "members", members } });
}

json ToolChatHistory(Player* bot, LlmChatLog const* chatLog, uint32 limit)
{
    json lines = json::array();
    if (!chatLog)
    {
        return json::object({ { "history", lines } });
    }
    for (auto const& line : chatLog->Recent(bot->GetGUID(), limit))
    {
        lines.push_back(json::object({
            { "speaker", line.speaker },
            { "text", line.text },
            { "channel", line.channel },
            { "was_me", line.fromBot } }));
    }
    return json::object({ { "history", lines } });
}

json ToolServerStatus(Player* bot)
{
    uint32 humans = 0;
    uint32 total = 0;
    for (auto const& pair : ObjectAccessor::GetPlayers())
    {
        Player* other = pair.second;
        if (!other || !other->IsInWorld())
        {
            continue;
        }
        ++total;
        // Every bot has a PlayerbotAI attached; a real client does not.
        if (!GET_PLAYERBOT_AI(other))
        {
            ++humans;
        }
    }

    return json::object({
        { "players_in_world", total },
        { "human_players", humans },
        { "your_name", bot->GetName() } });
}

} // namespace

std::vector<ToolSpec> const& SocialToolSpecs()
{
    static std::vector<ToolSpec> const specs = []
    {
        // Strict mode requires every property to be listed in `required` and
        // additionalProperties false, so tools that genuinely take nothing
        // declare an empty object.
        std::string const kNoArgs =
            R"({"type":"object","properties":{},"required":[],"additionalProperties":false})";

        std::vector<ToolSpec> out;
        out.push_back({ "get_character_sheet",
            "Your own race, class, level, health, mana, primary stats, money and current zone. "
            "Call this before saying anything about how you are doing or what you can handle.",
            kNoArgs });
        out.push_back({ "get_equipped_items",
            "The gear you are actually wearing, with item level and quality. Call this before "
            "commenting on your equipment.",
            kNoArgs });
        out.push_back({ "get_bag_items",
            "What is in your backpack and bags, with stack counts. Call this before claiming to "
            "have or not have an item.",
            kNoArgs });
        out.push_back({ "get_quest_log",
            "Your active quests, their level, and whether they are ready to turn in.",
            kNoArgs });
        out.push_back({ "get_nearby_players",
            "Players and bots within sight, with name, level, class and distance. Call this "
            "before referring to who is around you.",
            kNoArgs });
        out.push_back({ "get_group_members",
            "Whether you are in a party or raid, and who is in it.",
            kNoArgs });
        out.push_back({ "get_chat_history",
            "What was recently said to you and by you, newest first. Call this whenever someone "
            "refers to an earlier part of the conversation.",
            R"({"type":"object","properties":{"limit":{"type":"integer",)"
            R"("description":"How many lines to fetch, 1 to 40."}},)"
            R"("required":["limit"],"additionalProperties":false})" });
        out.push_back({ "get_server_status",
            "How many players and bots are currently in the world. Call this before answering "
            "any question about who or how many are online.",
            kNoArgs });
        return out;
    }();
    return specs;
}

std::string ExecuteTool(Player* bot, ToolCall const& call, LlmChatLog const* chatLog)
{
    if (!bot)
    {
        return ErrorObject("you are not in the world right now").dump();
    }

    json args = json::object();
    if (!call.arguments.empty())
    {
        try
        {
            args = json::parse(call.arguments);
        }
        catch (json::exception const&)
        {
            args = json::object(); // tolerate junk arguments, use defaults
        }
    }

    try
    {
        if (call.name == "get_character_sheet")
        {
            return ToolCharacterSheet(bot).dump();
        }
        if (call.name == "get_equipped_items")
        {
            return ToolEquippedItems(bot).dump();
        }
        if (call.name == "get_bag_items")
        {
            return ToolBagItems(bot).dump();
        }
        if (call.name == "get_quest_log")
        {
            return ToolQuestLog(bot).dump();
        }
        if (call.name == "get_nearby_players")
        {
            return ToolNearbyPlayers(bot).dump();
        }
        if (call.name == "get_group_members")
        {
            return ToolGroupMembers(bot).dump();
        }
        if (call.name == "get_chat_history")
        {
            uint32 limit = args.value("limit", 10u);
            limit = std::min<uint32>(std::max<uint32>(limit, 1u), 40u);
            return ToolChatHistory(bot, chatLog, limit).dump();
        }
        if (call.name == "get_server_status")
        {
            return ToolServerStatus(bot).dump();
        }
    }
    catch (std::exception const& e)
    {
        return ErrorObject(std::string("tool failed: ") + e.what()).dump();
    }

    return ErrorObject("unknown tool '" + call.name + "'").dump();
}

} // namespace LlmBots
