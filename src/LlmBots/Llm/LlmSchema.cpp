/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 */

#include "LlmSchema.h"

#include "json.hpp"

#include <mutex>
#include <set>

using nlohmann::json;

namespace LlmBots
{

namespace
{

std::mutex g_schemaMutex;
std::vector<std::pair<std::string, std::string>> g_schemas;

// The bot_utterance schema - Tier 1 social reactions.
char const* kBotUtteranceSchema = R"({
  "type": "object",
  "additionalProperties": false,
  "required": ["line", "emote", "channel"],
  "properties": {
    "line": { "type": "string" },
    "emote": {
      "type": ["string", "null"],
      "enum": ["laugh", "cheer", "cry", "rude", "point", "salute", "wave", "bow", "question", "applaud", "flex", "shy", null]
    },
    "channel": { "type": "string", "enum": ["say", "yell", "whisper", "party", "guild"] }
  }
})";

// The bot_agenda schema - Tier 2 deliberative planning.
char const* kBotAgendaSchema = R"({
  "type": "object",
  "additionalProperties": false,
  "required": ["goal", "zone", "social_stance", "strategy_toggles", "narrative", "reason"],
  "properties": {
    "goal": { "type": "string", "enum": ["grind", "travel", "idle", "socialize"] },
    "zone": { "type": ["string", "null"] },
    "social_stance": { "type": "string", "enum": ["helpful", "aloof", "seeking_group", "avoiding"] },
    "strategy_toggles": {
      "type": "array",
      "items": { "type": "string", "enum": ["+pvp", "-pvp", "+passive", "-passive"] }
    },
    "narrative": { "type": "string" },
    "reason": { "type": "string" }
  }
})";

// The chronicler_digest schema - observer commentary.
char const* kChroniclerDigestSchema = R"({
  "type": "object",
  "additionalProperties": false,
  "required": ["headline", "digest"],
  "properties": {
    "headline": { "type": "string" },
    "digest": { "type": "string" }
  }
})";

// --- minimal strict-mode JSON schema validator ---------------------------

std::string ValidateObject(json const& value, json const& schema); // fwd

bool TypeMatches(json const& value, json const& typeSpec)
{
    if (typeSpec.is_array())
    {
        for (auto const& t : typeSpec)
        {
            if (TypeMatches(value, t))
            {
                return true;
            }
        }
        return false;
    }

    std::string const type = typeSpec.get<std::string>();
    if (type == "string")
    {
        return value.is_string();
    }
    if (type == "integer")
    {
        return value.is_number_integer();
    }
    if (type == "number")
    {
        return value.is_number();
    }
    if (type == "boolean")
    {
        return value.is_boolean();
    }
    if (type == "null")
    {
        return value.is_null();
    }
    if (type == "array")
    {
        return value.is_array();
    }
    if (type == "object")
    {
        return value.is_object();
    }
    return false;
}

// Validates a value against one "properties" entry.
std::string ValidateProperty(json const& value, std::string const& propName, json const& spec)
{
    if (spec.contains("enum"))
    {
        bool matched = false;
        for (auto const& allowed : spec["enum"])
        {
            if (value == allowed)
            {
                matched = true;
                break;
            }
        }
        if (!matched)
        {
            return "property '" + propName + "' value not in enum";
        }
    }

    if (spec.contains("type") && !TypeMatches(value, spec["type"]))
    {
        return "property '" + propName + "' has wrong type";
    }

    // Nested object validation (arrays of objects: validate each element).
    if (spec.contains("properties"))
    {
        std::string problem = ValidateObject(value, spec);
        if (!problem.empty())
        {
            return problem;
        }
    }

    if (spec.contains("items"))
    {
        if (!value.is_array())
        {
            return "property '" + propName + "' is not an array";
        }
        for (auto const& item : value)
        {
            std::string problem = ValidateProperty(item, propName + "[]", spec["items"]);
            if (!problem.empty())
            {
                return problem;
            }
        }
    }

    return {};
}

std::string ValidateObject(json const& value, json const& schema)
{
    if (!value.is_object())
    {
        return "expected object";
    }

    if (schema.contains("required"))
    {
        for (auto const& req : schema["required"])
        {
            if (!value.contains(req.get<std::string>()))
            {
                return "missing required property '" + req.get<std::string>() + "'";
            }
        }
    }

    if (schema.contains("additionalProperties") && schema["additionalProperties"].get<bool>() == false)
    {
        for (auto const& [key, v] : value.items())
        {
            if (!schema.contains("properties") || !schema["properties"].contains(key))
            {
                return "unexpected property '" + key + "'";
            }
        }
    }

    if (schema.contains("properties"))
    {
        for (auto const& [key, spec] : schema["properties"].items())
        {
            if (value.contains(key))
            {
                std::string problem = ValidateProperty(value[key], key, spec);
                if (!problem.empty())
                {
                    return problem;
                }
            }
        }
    }

    return {};
}

} // namespace

std::string const& GetSchema(std::string_view name)
{
    static std::string const empty;
    std::lock_guard<std::mutex> guard(g_schemaMutex);
    for (auto const& [key, schema] : g_schemas)
    {
        if (key == name)
        {
            return schema;
        }
    }
    return empty;
}

std::vector<std::string> const& GetSchemaNames()
{
    static std::vector<std::string> names;
    std::lock_guard<std::mutex> guard(g_schemaMutex);
    if (!names.empty())
    {
        return names;
    }
    for (auto const& [key, schema] : g_schemas)
    {
        names.push_back(key);
    }
    return names;
}

void RegisterSchema(std::string name, std::string schema)
{
    std::lock_guard<std::mutex> guard(g_schemaMutex);
    g_schemas.emplace_back(std::move(name), std::move(schema));
}

namespace
{

struct SchemaRegistrar
{
    SchemaRegistrar()
    {
        RegisterSchema("bot_utterance", kBotUtteranceSchema);
        RegisterSchema("bot_agenda", kBotAgendaSchema);
        RegisterSchema("chronicler_digest", kChroniclerDigestSchema);
    }
};

static SchemaRegistrar g_schemaRegistrar;

} // namespace

std::vector<std::string> ValidateSchemasForStrictMode()
{
    std::vector<std::string> problems;

    for (auto const& [name, schemaStr] : g_schemas)
    {
        try
        {
            json schema = json::parse(schemaStr);
            if (!schema.contains("type") || schema["type"] != "object")
            {
                problems.push_back("schema '" + name + "': root must be type object");
                continue;
            }
            if (!schema.contains("additionalProperties") || schema["additionalProperties"] != false)
            {
                problems.push_back("schema '" + name + "': root must have additionalProperties:false");
            }
            if (!schema.contains("required"))
            {
                problems.push_back("schema '" + name + "': root must have a required list");
            }
            if (schema.contains("properties"))
            {
                for (auto const& [prop, spec] : schema["properties"].items())
                {
                    if (spec.contains("enum"))
                    {
                        continue; // closed enum: optionality expressed via union including null
                    }
                    if (!spec.contains("type"))
                    {
                        problems.push_back("schema '" + name + "': property '" + prop + "' has no type");
                    }
                }
            }
        }
        catch (json::exception const& e)
        {
            problems.push_back("schema '" + name + "' does not parse: " + std::string(e.what()));
        }
    }

    return problems;
}

std::string ValidateOutput(std::string_view schemaName, std::string const& content)
{
    std::string const schemaStr = GetSchema(schemaName);
    if (schemaStr.empty())
    {
        return "unknown schema '" + std::string(schemaName) + "'";
    }

    try
    {
        json schema = json::parse(schemaStr);
        json value = json::parse(content);
        return ValidateObject(value, schema);
    }
    catch (json::exception const& e)
    {
        return std::string("output does not parse: ") + e.what();
    }
}

} // namespace LlmBots
