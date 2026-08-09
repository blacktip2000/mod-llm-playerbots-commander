/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 *
 * Abstract LLM provider. Implementations are registered in a small factory
 * keyed by the LlmBots.Provider config string.
 */

#ifndef LLM_PROVIDER_H
#define LLM_PROVIDER_H

#include "LlmTypes.h"

#include <string>

namespace LlmBots
{

class LlmProvider
{
public:
    virtual ~LlmProvider() = default;

    virtual std::string Name() const = 0;

    // Validate configuration. Called once at startup; a failure logs and the
    // module degrades to plain playerbots.
    virtual bool Configure(ProviderConfig const& config, std::string& err) = 0;

    // Blocking call - may only run on worker threads. Must not touch game
    // state.
    virtual LlmResponse Complete(LlmRequest const& request) = 0;
};

// Factory: returns nullptr on unknown provider name.
LlmProvider* CreateProvider(std::string const& name);

} // namespace LlmBots

#endif // LLM_PROVIDER_H
