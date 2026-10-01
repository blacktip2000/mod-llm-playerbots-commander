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

#include "LlmToolbox.h"

#include "LlmChatLog.h"

#include "Bag.h"
#include "DBCStores.h"
#include "Group.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Event.h"
#include "ExternalEventHelper.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "QuestDef.h"
#include "SharedDefines.h"
#include "World.h"

#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

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

// Session-only raid role assignments, separated by human account.
// These are intentionally not persisted to the database.
struct RaidRoleEntry
{
    std::string target;
    std::string role;
};

struct AccountRaidRoles
{
    uint32 accountId = 0;
    std::vector<RaidRoleEntry> roles;
};

std::vector<AccountRaidRoles> gRaidRolesByAccount;

std::vector<RaidRoleEntry>& RaidRolesForAccount(uint32 accountId)
{
    for (AccountRaidRoles& table : gRaidRolesByAccount)
    {
        if (table.accountId == accountId)
        {
            return table.roles;
        }
    }

    gRaidRolesByAccount.push_back({ accountId, {} });
    return gRaidRolesByAccount.back().roles;
}

std::string TrimRaidRoleText(std::string value)
{
    auto const notSpace = [](unsigned char c)
    {
        return !std::isspace(c);
    };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::string CanonicalRaidRole(std::string const& input)
{
    std::string const role = TrimRaidRoleText(input);

    if (role == "메인탱커" || role == "main_tank")
        return "main_tank";
    if (role == "오프탱커" || role == "서브탱커" || role == "off_tank")
        return "off_tank";

    if (role == "무기딜러" || role == "arms_dps")
        return "arms_dps";
    if (role == "분노딜러" || role == "fury_dps")
        return "fury_dps";

    if (role == "신성힐러" || role == "holy_healer")
        return "holy_healer";
    if (role == "수양힐러" || role == "discipline_healer")
        return "discipline_healer";
    if (role == "복원힐러" || role == "restoration_healer")
        return "restoration_healer";

    if (role == "징벌딜러" || role == "retribution_dps")
        return "retribution_dps";

    if (role == "야수딜러" || role == "beast_mastery_dps")
        return "beast_mastery_dps";
    if (role == "사격딜러" || role == "marksmanship_dps")
        return "marksmanship_dps";
    if (role == "생존딜러" || role == "survival_dps")
        return "survival_dps";

    if (role == "암살딜러" || role == "assassination_dps")
        return "assassination_dps";
    if (role == "전투딜러" || role == "combat_dps")
        return "combat_dps";
    if (role == "잠행딜러" || role == "subtlety_dps")
        return "subtlety_dps";

    if (role == "암흑딜러" || role == "shadow_dps")
        return "shadow_dps";

    if (role == "혈기딜러" || role == "blood_dps")
        return "blood_dps";
    if (role == "냉기딜러" || role == "frost_dps")
        return "frost_dps";
    if (role == "부정딜러" || role == "unholy_dps")
        return "unholy_dps";

    if (role == "정기딜러" || role == "elemental_dps")
        return "elemental_dps";
    if (role == "고양딜러" || role == "enhancement_dps")
        return "enhancement_dps";

    if (role == "비전딜러" || role == "arcane_dps")
        return "arcane_dps";
    if (role == "화염딜러" || role == "fire_dps")
        return "fire_dps";
    if (role == "냉불딜러" || role == "frostfire_dps")
        return "frostfire_dps";

    if (role == "고통딜러" || role == "affliction_dps")
        return "affliction_dps";
    if (role == "악마딜러" || role == "demonology_dps")
        return "demonology_dps";
    if (role == "파괴딜러" || role == "destruction_dps")
        return "destruction_dps";

    if (role == "조화딜러" || role == "balance_dps")
        return "balance_dps";
    if (role == "야성딜러" || role == "표범딜러" || role == "cat_dps")
        return "cat_dps";
    if (role == "곰탱커" || role == "bear_tank")
        return "bear_tank";

    return "";
}

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

json ToolFindPlayerbot(Player* issuer, std::string const& className, uint32 minLevel, uint32 maxLevel,
    float maxDistance, uint32 limit)
{
    if (!issuer)
    {
        return ErrorObject("find_playerbot requires a human issuer speaking to the commander");
    }

    uint8 wantedClass = 0;
    if (!className.empty())
    {
        if (className == "warrior")
            wantedClass = CLASS_WARRIOR;
        else if (className == "paladin")
            wantedClass = CLASS_PALADIN;
        else if (className == "hunter")
            wantedClass = CLASS_HUNTER;
        else if (className == "rogue")
            wantedClass = CLASS_ROGUE;
        else if (className == "priest")
            wantedClass = CLASS_PRIEST;
        else if (className == "death_knight")
            wantedClass = CLASS_DEATH_KNIGHT;
        else if (className == "shaman")
            wantedClass = CLASS_SHAMAN;
        else if (className == "mage")
            wantedClass = CLASS_MAGE;
        else if (className == "warlock")
            wantedClass = CLASS_WARLOCK;
        else if (className == "druid")
            wantedClass = CLASS_DRUID;
        else
            return ErrorObject("unknown playerbot class '" + className + "'");
    }

    minLevel = std::max<uint32>(1u, std::min<uint32>(minLevel, 80u));
    maxLevel = std::max<uint32>(1u, std::min<uint32>(maxLevel, 80u));
    if (minLevel > maxLevel)
        std::swap(minLevel, maxLevel);

    maxDistance = std::max<float>(1.0f, std::min<float>(maxDistance, 5000.0f));
    limit = std::max<uint32>(1u, std::min<uint32>(limit, 10u));

    Map* map = issuer->GetMap();
    if (!map)
    {
        return ErrorObject("human issuer is not on a valid map");
    }

    std::vector<std::pair<float, Player*>> candidates;
    for (auto const& pair : ObjectAccessor::GetPlayers())
    {
        Player* other = pair.second;
        if (!other || !other->IsInWorld() || other->GetMap() != map)
            continue;

        if (!sRandomPlayerbotMgr.IsRandomBot(other))
            continue;

        if (other->GetTeamId() != issuer->GetTeamId())
            continue;

        if (wantedClass && other->getClass() != wantedClass)
            continue;

        uint32 const level = other->GetLevel();
        if (level < minLevel || level > maxLevel)
            continue;

        float const distance = issuer->GetDistance2d(other);
        if (distance > maxDistance)
            continue;

        candidates.emplace_back(distance, other);
    }

    std::sort(candidates.begin(), candidates.end(),
        [](auto const& left, auto const& right) { return left.first < right.first; });

    json matches = json::array();
    for (uint32 i = 0; i < candidates.size() && i < limit; ++i)
    {
        Player* found = candidates[i].second;
        matches.push_back(json::object({
            { "name", found->GetName() },
            { "class", ClassName(found->getClass()) },
            { "level", found->GetLevel() },
            { "yards_away", static_cast<uint32>(candidates[i].first) },
            { "alive", found->IsAlive() },
            { "grouped", found->GetGroup() != nullptr }
        }));
    }

    return json::object({ { "matches", matches } });
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

std::vector<ToolSpec> const& CommanderToolSpecs()
{
    static std::vector<ToolSpec> const specs = []
    {
        std::vector<ToolSpec> out;
        out.push_back({ "playerbot_command",
            "Command one online Playerbot on behalf of the human speaking to you. Choose the target "
            "bot by its exact character name and translate the human's natural-language instruction "
            "into one existing playerbot chat command. Do not use this tool for ordinary conversation.",
            R"({"type":"object","properties":{"target":{"type":"string",)"
            R"("description":"Exact character name of the online Playerbot to command."},)"
            R"("command":{"type":"string",)"
            R"("description":"One playerbot chat command to execute, for example follow, stay, attack or flee."}},)"
            R"("required":["target","command"],"additionalProperties":false})" });
        out.push_back({ "find_playerbot",
            "Find online random Playerbots near the human owner. Use this when the human asks you to "
            "find, locate, choose or look for a bot by class, level or proximity. Results are restricted "
            "to random Playerbots on the human's faction and current map. Always supply every argument; "
            "use an empty string for class when any class is acceptable.",
            R"({"type":"object","properties":{)"
            R"("class":{"type":"string","enum":["","warrior","paladin","hunter","rogue","priest","death_knight","shaman","mage","warlock","druid"],)"
            R"("description":"Class filter. Use an empty string when any class is acceptable."},)"
            R"("min_level":{"type":"integer","minimum":1,"maximum":80,"description":"Minimum level."},)"
            R"("max_level":{"type":"integer","minimum":1,"maximum":80,"description":"Maximum level."},)"
            R"("max_distance":{"type":"integer","minimum":1,"maximum":5000,)"
            R"("description":"Maximum distance from the human owner in yards."},)"
            R"("limit":{"type":"integer","minimum":1,"maximum":10,)"
            R"("description":"Maximum number of matching bots to return."}},)"
            R"("required":["class","min_level","max_level","max_distance","limit"],)"
            R"("additionalProperties":false})" });
        out.push_back({ "join_playerbot_to_group",
            "Invite one online random Playerbot into the human owner's group. "
            "Use this when the human asks to add, invite or bring a specific bot into their party. "
            "The human owner remains the group leader when creating a new group.",
            R"({"type":"object","properties":{)"
            R"("target":{"type":"string","description":"Exact character name of the random Playerbot to invite."}},)"
            R"("required":["target"],"additionalProperties":false})" });
        out.push_back({ "teleport_owner_to_playerbot",
            "Teleport the human owner speaking to you to the location of one online Playerbot. "
            "Use this when the human asks to teleport, move or send themselves to a specific bot. "
            "This tool moves only the human owner; it never teleports the bot.",
            R"({"type":"object","properties":{)"
            R"("target":{"type":"string","description":"Exact character name of the online Playerbot whose location is the destination."}},)"
            R"("required":["target"],"additionalProperties":false})" });
        out.push_back({ "teleport_owner_to_city",
            "Teleport the human owner speaking to you to one supported capital city using the server's game_tele destination. "
            "Use this only when the human explicitly asks to travel or teleport to a supported city.",
            R"({"type":"object","properties":{)"
            R"("city":{"type":"string","enum":["stormwind","ironforge","darnassus","exodar","orgrimmar","undercity","thunder_bluff","silvermoon"],)"
            R"("description":"Supported capital city destination."}},)"
            R"("required":["city"],"additionalProperties":false})" });
        out.push_back({ "set_raid_roles",
            "Store raid role assignments for the human owner's current server session. "
            "Use this when the human sends a ROLE list such as "
            "'ROLE CharacterA=메인탱커; CharacterB=분노딜러'. "
            "Pass everything after ROLE as one entries string. "
            "This tool only stores assignments and must not change character level, talents or equipment.",
            R"({"type":"object","properties":{)"
            R"("entries":{"type":"string","description":"Semicolon-separated character=role assignments exactly as supplied by the human after ROLE."}},)"
            R"("required":["entries"],"additionalProperties":false})" });
        out.push_back({ "list_raid_roles",
            "Read the raid role assignments currently stored for the human owner's server session. "
            "Use this when the human asks for the raid roster, saved raid roles, registered raid members, "
            "or when you need to know which characters and roles were previously stored.",
            R"({"type":"object","properties":{},"required":[],"additionalProperties":false})" });
        out.push_back({ "prepare_raid_character",
            "Prepare one online Playerbot character for a raid role. "
            "Use the exact character name. Translate the human's Korean or English role description into one canonical role below. "
            "Roles are intentionally specific; never guess between different talent specializations.",
            R"({"type":"object","properties":{)"
            R"("target":{"type":"string","description":"Exact character name of the online Playerbot to prepare."},)"
            R"("role":{"type":"string","enum":[)"
            R"("main_tank","off_tank",)"
            R"("arms_dps","fury_dps",)"
            R"("holy_healer","discipline_healer","restoration_healer",)"
            R"("retribution_dps",)"
            R"("beast_mastery_dps","marksmanship_dps","survival_dps",)"
            R"("assassination_dps","combat_dps","subtlety_dps",)"
            R"("shadow_dps",)"
            R"("blood_dps","frost_dps","unholy_dps",)"
            R"("elemental_dps","enhancement_dps",)"
            R"("arcane_dps","fire_dps","frostfire_dps",)"
            R"("affliction_dps","demonology_dps","destruction_dps",)"
            R"("balance_dps","cat_dps","bear_tank"],)"
            R"("description":"Exact raid role and talent specialization. Main/off tank are distinguished as raid assignments even when they use the same tank talent preset."},)"
            R"("level":{"type":"integer","minimum":1,"maximum":80,"description":"Target character level for preparation."}},)"
            R"("required":["target","role","level"],"additionalProperties":false})" });
        return out;
    }();
    return specs;
}

std::string ExecuteTool(Player* bot, ToolCall const& call, LlmChatLog const* chatLog, Player* issuer)
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
        if (call.name == "find_playerbot")
        {
            if (!issuer)
            {
                return ErrorObject("find_playerbot requires a human issuer speaking to the commander").dump();
            }

            std::string const className = args.value("class", "");
            uint32 const minLevel = args.value("min_level", 1u);
            uint32 const maxLevel = args.value("max_level", 80u);
            float const maxDistance = args.value("max_distance", 1000.0f);
            uint32 const limit = args.value("limit", 5u);

            return ToolFindPlayerbot(issuer, className, minLevel, maxLevel, maxDistance, limit).dump();
        }
        if (call.name == "join_playerbot_to_group")
        {
            if (!issuer)
            {
                return ErrorObject("join_playerbot_to_group requires a human issuer speaking to the commander").dump();
            }

            std::string const targetName = args.value("target", "");
            if (targetName.empty())
            {
                return ErrorObject("join_playerbot_to_group requires target").dump();
            }

            Player* target = ObjectAccessor::FindPlayerByName(targetName, false);
            if (!target)
            {
                return ErrorObject("target player '" + targetName + "' is not online").dump();
            }

            PlayerbotAI* targetAi = GET_PLAYERBOT_AI(target);
            if (!targetAi || !targetAi->GetAiObjectContext())
            {
                return ErrorObject("target '" + targetName + "' is not an active Playerbot").dump();
            }

            if (!sRandomPlayerbotMgr.IsRandomBot(target))
            {
                return ErrorObject("target '" + targetName + "' is not a random Playerbot").dump();
            }

            if (target->GetTeamId() != issuer->GetTeamId())
            {
                return ErrorObject("target '" + targetName + "' is not on the human owner's faction").dump();
            }

            Group* issuerGroup = issuer->GetGroup();
            if (issuerGroup && target->GetGroup() == issuerGroup)
            {
                return json::object({
                    { "ok", true },
                    { "already_in_group", true },
                    { "target", target->GetName() },
                    { "leader", issuerGroup->GetLeaderGUID() == issuer->GetGUID() ? issuer->GetName() : "" }
                }).dump();
            }

            Event joinEvent("join", issuer->GetGUID(), issuer);
            if (!targetAi->DoSpecificAction("join", joinEvent, true))
            {
                return ErrorObject("Playerbot join action rejected the request for '" + targetName + "'").dump();
            }

            return json::object({
                { "ok", true },
                { "invite_issued", true },
                { "target", target->GetName() },
                { "issuer", issuer->GetName() }
            }).dump();
        }
        if (call.name == "teleport_owner_to_playerbot")
        {
            if (!issuer)
            {
                return ErrorObject("teleport_owner_to_playerbot requires a human issuer speaking to the commander").dump();
            }

            std::string const targetName = args.value("target", "");
            if (targetName.empty())
            {
                return ErrorObject("teleport_owner_to_playerbot requires target").dump();
            }

            Player* target = ObjectAccessor::FindPlayerByName(targetName, false);
            if (!target)
            {
                return ErrorObject("target player '" + targetName + "' is not online").dump();
            }

            PlayerbotAI* targetAi = GET_PLAYERBOT_AI(target);
            if (!targetAi || !targetAi->GetAiObjectContext())
            {
                return ErrorObject("target '" + targetName + "' is not an active Playerbot").dump();
            }

            if (target->GetTeamId() != issuer->GetTeamId())
            {
                return ErrorObject("target '" + targetName + "' is not on the human owner's faction").dump();
            }

            if (!issuer->TeleportTo(
                    target->GetMapId(),
                    target->GetPositionX(),
                    target->GetPositionY(),
                    target->GetPositionZ(),
                    target->GetOrientation()))
            {
                return ErrorObject("teleport to '" + targetName + "' failed").dump();
            }

            return json::object({
                { "ok", true },
                { "target", target->GetName() },
                { "issuer", issuer->GetName() },
                { "map_id", target->GetMapId() }
            }).dump();
        }
        if (call.name == "teleport_owner_to_city")
        {
            if (!issuer)
            {
                return ErrorObject("teleport_owner_to_city requires a human issuer speaking to the commander").dump();
            }

            std::string const city = args.value("city", "");
            char const* teleName = nullptr;

            if (city == "stormwind")
                teleName = "Stormwind";
            else if (city == "ironforge")
                teleName = "Ironforge";
            else if (city == "darnassus")
                teleName = "Darnassus";
            else if (city == "exodar")
                teleName = "TheExodar";
            else if (city == "orgrimmar")
                teleName = "Orgrimmar";
            else if (city == "undercity")
                teleName = "Undercity";
            else if (city == "thunder_bluff")
                teleName = "ThunderBluff";
            else if (city == "silvermoon")
                teleName = "SilvermoonCity";
            else
                return ErrorObject("unsupported city '" + city + "'").dump();

            GameTele const* tele = sObjectMgr->GetGameTele(teleName, true);
            if (!tele)
            {
                return ErrorObject("game_tele destination '" + std::string(teleName) + "' was not found").dump();
            }

            if (!issuer->TeleportTo(
                    tele->mapId,
                    tele->position_x,
                    tele->position_y,
                    tele->position_z,
                    tele->orientation))
            {
                return ErrorObject("teleport to city '" + city + "' failed").dump();
            }

            return json::object({
                { "ok", true },
                { "city", city },
                { "destination", tele->name },
                { "issuer", issuer->GetName() },
                { "map_id", tele->mapId }
            }).dump();
        }
        if (call.name == "list_raid_roles")
        {
            if (!issuer || !issuer->GetSession())
            {
                return ErrorObject("list_raid_roles requires a human issuer speaking to the commander").dump();
            }

            uint32 const accountId = issuer->GetSession()->GetAccountId();
            std::vector<RaidRoleEntry>& stored = RaidRolesForAccount(accountId);

            json assignments = json::array();
            for (RaidRoleEntry const& entry : stored)
            {
                assignments.push_back(json::object({
                    { "target", entry.target },
                    { "role", entry.role }
                }));
            }

            return json::object({
                { "ok", true },
                { "total", stored.size() },
                { "assignments", assignments }
            }).dump();
        }

        if (call.name == "set_raid_roles")
        {
            if (!issuer || !issuer->GetSession())
            {
                return ErrorObject("set_raid_roles requires a human issuer speaking to the commander").dump();
            }

            std::string const entriesText = args.value("entries", "");
            if (TrimRaidRoleText(entriesText).empty())
            {
                return ErrorObject("set_raid_roles requires at least one character=role assignment").dump();
            }

            std::vector<RaidRoleEntry> pending;
            size_t start = 0;

            while (start <= entriesText.size())
            {
                size_t const separator = entriesText.find(';', start);
                std::string entry = separator == std::string::npos
                    ? entriesText.substr(start)
                    : entriesText.substr(start, separator - start);

                entry = TrimRaidRoleText(entry);

                if (!entry.empty())
                {
                    size_t const equals = entry.find('=');
                    if (equals == std::string::npos || entry.find('=', equals + 1) != std::string::npos)
                    {
                        return ErrorObject("invalid raid role assignment '" + entry +
                            "'; expected character=role").dump();
                    }

                    std::string const target = TrimRaidRoleText(entry.substr(0, equals));
                    std::string const roleText = TrimRaidRoleText(entry.substr(equals + 1));

                    if (target.empty() || roleText.empty())
                    {
                        return ErrorObject("invalid raid role assignment '" + entry +
                            "'; character and role must both be present").dump();
                    }

                    std::string const canonicalRole = CanonicalRaidRole(roleText);
                    if (canonicalRole.empty())
                    {
                        return ErrorObject("unsupported raid role '" + roleText +
                            "' for character '" + target + "'").dump();
                    }

                    bool replacedPending = false;
                    for (RaidRoleEntry& pendingEntry : pending)
                    {
                        if (pendingEntry.target == target)
                        {
                            pendingEntry.role = canonicalRole;
                            replacedPending = true;
                            break;
                        }
                    }

                    if (!replacedPending)
                    {
                        pending.push_back({ target, canonicalRole });
                    }
                }

                if (separator == std::string::npos)
                {
                    break;
                }

                start = separator + 1;
            }

            if (pending.empty())
            {
                return ErrorObject("set_raid_roles did not contain any valid assignments").dump();
            }

            uint32 const accountId = issuer->GetSession()->GetAccountId();
            std::vector<RaidRoleEntry>& stored = RaidRolesForAccount(accountId);

            for (RaidRoleEntry const& pendingEntry : pending)
            {
                bool replacedStored = false;

                for (RaidRoleEntry& storedEntry : stored)
                {
                    if (storedEntry.target == pendingEntry.target)
                    {
                        storedEntry.role = pendingEntry.role;
                        replacedStored = true;
                        break;
                    }
                }

                if (!replacedStored)
                {
                    stored.push_back(pendingEntry);
                }
            }

            json assignments = json::array();
            for (RaidRoleEntry const& entry : stored)
            {
                assignments.push_back(json::object({
                    { "target", entry.target },
                    { "role", entry.role }
                }));
            }

            return json::object({
                { "ok", true },
                { "updated", pending.size() },
                { "total", stored.size() },
                { "assignments", assignments }
            }).dump();
        }

        if (call.name == "prepare_raid_character")
        {
            if (!issuer)
            {
                return ErrorObject("prepare_raid_character requires a human issuer speaking to the commander").dump();
            }

            std::string const targetName = args.value("target", "");
            std::string const role = args.value("role", "");
            uint32 const level = args.value("level", 0u);

            if (targetName.empty() || role.empty() || level < 1 || level > 80)
            {
                return ErrorObject("prepare_raid_character requires valid target, role and level").dump();
            }

            Player* target = ObjectAccessor::FindPlayerByName(targetName, false);
            if (!target)
            {
                return ErrorObject("target player '" + targetName + "' is not online").dump();
            }

            PlayerbotAI* targetAi = GET_PLAYERBOT_AI(target);
            if (!targetAi || !targetAi->GetAiObjectContext())
            {
                return ErrorObject("target '" + targetName + "' is not an active Playerbot").dump();
            }

            if (!targetAi->IsAltBot())
            {
                return ErrorObject("target '" + targetName + "' is not an Altbot").dump();
            }

            if (targetAi->GetMaster() != issuer)
            {
                return ErrorObject("target '" + targetName + "' is not controlled by the human issuer").dump();
            }

            std::string spec;

            switch (target->getClass())
            {
                case CLASS_WARRIOR:
                    if (role == "main_tank" || role == "off_tank")
                        spec = "prot pve";
                    else if (role == "arms_dps")
                        spec = "arms pve";
                    else if (role == "fury_dps")
                        spec = "fury pve";
                    break;

                case CLASS_PALADIN:
                    if (role == "main_tank" || role == "off_tank")
                        spec = "prot pve";
                    else if (role == "holy_healer")
                        spec = "holy pve";
                    else if (role == "retribution_dps")
                        spec = "ret pve";
                    break;

                case CLASS_HUNTER:
                    if (role == "beast_mastery_dps")
                        spec = "bm pve";
                    else if (role == "marksmanship_dps")
                        spec = "mm pve";
                    else if (role == "survival_dps")
                        spec = "surv pve";
                    break;

                case CLASS_ROGUE:
                    if (role == "assassination_dps")
                        spec = "as pve";
                    else if (role == "combat_dps")
                        spec = "combat pve";
                    else if (role == "subtlety_dps")
                        spec = "subtlety pve";
                    break;

                case CLASS_PRIEST:
                    if (role == "discipline_healer")
                        spec = "disc pve";
                    else if (role == "holy_healer")
                        spec = "holy pve";
                    else if (role == "shadow_dps")
                        spec = "shadow pve";
                    break;

                case CLASS_DEATH_KNIGHT:
                    if (role == "blood_dps")
                        spec = "blood pve";
                    else if (role == "frost_dps")
                        spec = "frost pve";
                    else if (role == "unholy_dps")
                        spec = "unholy pve";
                    break;

                case CLASS_SHAMAN:
                    if (role == "elemental_dps")
                        spec = "ele pve";
                    else if (role == "enhancement_dps")
                        spec = "enh pve";
                    else if (role == "restoration_healer")
                        spec = "resto pve";
                    break;

                case CLASS_MAGE:
                    if (role == "arcane_dps")
                        spec = "arcane pve";
                    else if (role == "fire_dps")
                        spec = "fire pve";
                    else if (role == "frost_dps")
                        spec = "frost pve";
                    else if (role == "frostfire_dps")
                        spec = "frostfire pve";
                    break;

                case CLASS_WARLOCK:
                    if (role == "affliction_dps")
                        spec = "affli pve";
                    else if (role == "demonology_dps")
                        spec = "demo pve";
                    else if (role == "destruction_dps")
                        spec = "destro pve";
                    break;

                case CLASS_DRUID:
                    if (role == "main_tank" || role == "off_tank" || role == "bear_tank")
                        spec = "bear pve";
                    else if (role == "restoration_healer")
                        spec = "resto pve";
                    else if (role == "balance_dps")
                        spec = "balance pve";
                    else if (role == "cat_dps")
                        spec = "cat pve";
                    break;

                default:
                    break;
            }

            if (spec.empty())
            {
                return ErrorObject(
                    "role '" + role + "' is not supported for target '" +
                    targetName + "' class").dump();
            }

            uint32 const oldLevel = target->GetLevel();

            if (oldLevel != level)
            {
                target->CombatStop(true);
                target->GiveLevel(level);
                target->SetUInt32Value(PLAYER_XP, 0);
                target->InitStatsForLevel(true);
            }

            Event talentEvent("talents", "spec " + spec, issuer);
            bool const talentsHandled =
                targetAi->DoSpecificAction("talents", talentEvent, true);

            Event maintenanceEvent("maintenance", "", issuer);
            bool const maintenanceHandled =
                targetAi->DoSpecificAction("maintenance", maintenanceEvent, true);

            Event autogearEvent("autogear", "", issuer);
            bool const autogearHandled =
                targetAi->DoSpecificAction("autogear", autogearEvent, true);

            return json::object({
                { "ok", talentsHandled && maintenanceHandled && autogearHandled },
                { "target", target->GetName() },
                { "role", role },
                { "spec", spec },
                { "old_level", oldLevel },
                { "level", target->GetLevel() },
                { "talents_handled", talentsHandled },
                { "maintenance_handled", maintenanceHandled },
                { "autogear_handled", autogearHandled },
                { "issuer", issuer->GetName() }
            }).dump();
        }

        if (call.name == "playerbot_command")
        {
            if (!issuer)
            {
                return ErrorObject("playerbot_command requires a human issuer speaking to the commander").dump();
            }

            std::string const targetName = args.value("target", "");
            std::string const command = args.value("command", "");
            if (targetName.empty() || command.empty())
            {
                return ErrorObject("playerbot_command requires both target and command").dump();
            }

            Player* target = ObjectAccessor::FindPlayerByName(targetName, false);
            if (!target)
            {
                return ErrorObject("target player '" + targetName + "' is not online").dump();
            }

            PlayerbotAI* targetAi = GET_PLAYERBOT_AI(target);
            if (!targetAi || !targetAi->GetAiObjectContext())
            {
                return ErrorObject("target '" + targetName + "' is not an active Playerbot").dump();
            }

            ExternalEventHelper helper(targetAi->GetAiObjectContext());
            bool const handled = helper.ParseChatCommand(command, issuer);

            return json::object({
                { "ok", handled },
                { "target", target->GetName() },
                { "command", command },
                { "issuer", issuer->GetName() }
            }).dump();
        }
    }
    catch (std::exception const& e)
    {
        return ErrorObject(std::string("tool failed: ") + e.what()).dump();
    }

    return ErrorObject("unknown tool '" + call.name + "'").dump();
}

} // namespace LlmBots
