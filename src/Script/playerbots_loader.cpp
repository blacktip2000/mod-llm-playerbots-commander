/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// From SC
void AddPlayerbotsScripts();

// LLM bots (src/LlmBots). Registers its own PlayerScript/WorldScript/
// CommandScript; inert unless LlmBots.Enable = 1.
void AddLlmBotsScripts();

// Add all
void Addmod_playerbotsScripts()
{
    AddPlayerbotsScripts();
    AddLlmBotsScripts();
}
