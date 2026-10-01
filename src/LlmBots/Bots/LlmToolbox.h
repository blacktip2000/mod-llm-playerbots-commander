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

// Action tool declarations available only to the configured commander brain.
// Ordinary roster bots receive only the read-only social tools.
std::vector<ToolSpec> const& CommanderToolSpecs();

// Run one tool for a live LLM bot. For commander tools, issuer is the human
// player who gave the natural-language instruction to the commander bot.
// Never throws: failures are returned as JSON error objects.
std::string ExecuteTool(Player* bot, ToolCall const& call, LlmChatLog const* chatLog, Player* issuer);

} // namespace LlmBots

#endif // LLM_TOOLBOX_H
