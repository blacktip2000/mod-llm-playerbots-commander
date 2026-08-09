/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 *
 * The tools a bot may call to look at the world.
 *
 * Threading: LlmProvider::Complete runs on a worker thread and must never
 * touch game state, so tools are NOT executed there. The provider returns the
 * calls the model asked for, the world thread runs them here against a live
 * Player*, and LlmClient::Resume feeds the results back. Everything in this
 * file is world-thread only.
 */

#ifndef LLM_TOOLBOX_H
#define LLM_TOOLBOX_H

#include "LlmTypes.h"

#include <string>
#include <vector>

class Player;

namespace LlmBots
{

class LlmChatLog;

// The tool declarations sent with every social call. Stable for the process
// lifetime; built once on first use.
std::vector<ToolSpec> const& SocialToolSpecs();

// Run one tool against a live bot and return its result as a JSON string.
// Never throws: an unknown tool or a bad argument object comes back as a JSON
// object with an "error" field, which the model can read and recover from.
std::string ExecuteTool(Player* bot, ToolCall const& call, LlmChatLog const* chatLog);

} // namespace LlmBots

#endif // LLM_TOOLBOX_H
