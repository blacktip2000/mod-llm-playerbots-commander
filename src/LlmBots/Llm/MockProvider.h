/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 *
 * Deterministic fake provider: returns canned but schema-valid responses with
 * zero network. Used for offline testing of the whole pipeline and as a
 * no-cost fallback. Optionally fails a percentage of calls to exercise the
 * retry / fallback paths.
 */

#ifndef MOCK_PROVIDER_H
#define MOCK_PROVIDER_H

#include "LlmProvider.h"

namespace LlmBots
{

class MockProvider : public LlmProvider
{
public:
    std::string Name() const override;
    bool Configure(ProviderConfig const& config, std::string& err) override;
    LlmResponse Complete(LlmRequest const& request) override;

private:
    uint32_t m_failChancePct = 0;
    uint32_t m_callCount = 0;
};

} // namespace LlmBots

#endif // MOCK_PROVIDER_H
