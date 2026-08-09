/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 *
 * JSON Schemas for structured-output calls, plus strict local validation.
 *
 * The schemas are written to satisfy the strict-mode contract of the Foundry
 * / OpenAI structured-output API: every object carries additionalProperties
 * false, and every property is listed in required (optionality is expressed
 * with a ["string","null"] type union, never by omission).
 */

#ifndef LLM_SCHEMA_H
#define LLM_SCHEMA_H

#include <string>
#include <string_view>
#include <vector>

namespace LlmBots
{

// The JSON Schema document for a named schema, as a string.
std::string const& GetSchema(std::string_view name);

// Returns the list of registered schema names.
std::vector<std::string> const& GetSchemaNames();

// Registers a schema (called from LlmSchemaInit.cpp).
void RegisterSchema(std::string name, std::string schema);

// Startup self-check: verifies every registered schema satisfies the strict
// mode contract. Returns a list of problems; empty = all schemas valid.
std::vector<std::string> ValidateSchemasForStrictMode();

// Validates model output against a registered schema. Returns empty string on
// success, else a description of the first problem.
std::string ValidateOutput(std::string_view schemaName, std::string const& content);

} // namespace LlmBots

#endif // LLM_SCHEMA_H
