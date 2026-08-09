/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 */

#include "LlmProvider.h"

#include "FoundryProvider.h"
#include "MockProvider.h"
#include "OpenAiProvider.h"

namespace LlmBots
{

LlmProvider* CreateProvider(std::string const& name)
{
    if (name == "openai")
    {
        return new OpenAiProvider();
    }
    if (name == "foundry")
    {
        return new FoundryProvider();
    }
    if (name == "mock")
    {
        return new MockProvider();
    }
    return nullptr;
}

} // namespace LlmBots
