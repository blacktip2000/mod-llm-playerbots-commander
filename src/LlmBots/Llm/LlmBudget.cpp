/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 */

#include "LlmBudget.h"

#include <algorithm>

namespace LlmBots
{

void LlmBudget::Configure(uint32_t maxCallsPerMinute, uint32_t maxTokensPerHour, uint32_t maxQueueDepth)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_maxCallsPerMinute = maxCallsPerMinute;
    m_maxTokensPerHour = maxTokensPerHour;
    m_maxQueueDepth = maxQueueDepth;
    m_bucketTokens = maxCallsPerMinute; // start full
    m_bucketRefill = std::chrono::steady_clock::now();
}

BudgetResult LlmBudget::Check(Purpose /*purpose*/, ModelTier tier)
{
    if (!m_enabled.load())
    {
        return BudgetResult::Disabled;
    }

    std::lock_guard<std::mutex> guard(m_mutex);

    if (m_maxCallsPerMinute > 0)
    {
        auto const now = std::chrono::steady_clock::now();
        double const elapsedMin = std::chrono::duration<double>(now - m_bucketRefill).count() / 60.0;
        m_bucketTokens = static_cast<uint32_t>(
            std::min<uint64_t>(m_maxCallsPerMinute, m_bucketTokens + static_cast<uint64_t>(elapsedMin * m_maxCallsPerMinute)));
        m_bucketRefill = now;

        if (m_bucketTokens <= 0)
        {
            ++m_stats.refused;
            return BudgetResult::CallRateExceeded;
        }
        --m_bucketTokens;
    }

    if (m_maxTokensPerHour > 0)
    {
        // Rolling one-hour window.
        auto const now = std::chrono::steady_clock::now();
        double const elapsedH = std::chrono::duration<double>(now - m_hourStart).count() / 3600.0;
        if (elapsedH >= 1.0)
        {
            m_tokensThisHour = 0;
            m_hourStart = now;
        }

        uint32_t const threshold = m_maxTokensPerHour;
        if (m_tokensThisHour >= threshold)
        {
            ++m_stats.refused;
            return BudgetResult::TokenBudgetExceeded;
        }
        if (m_tokensThisHour >= threshold * 8 / 10 && tier == ModelTier::Think)
        {
            return BudgetResult::Degraded;
        }
    }

    if (m_maxQueueDepth > 0 && m_queued.load() >= m_maxQueueDepth)
    {
        ++m_stats.refused;
        return BudgetResult::CallRateExceeded; // treated as a refusal
    }

    return BudgetResult::Allowed;
}

void LlmBudget::OnQueued()
{
    ++m_queued;
}

void LlmBudget::OnDequeued()
{
    if (m_queued.load() > 0)
    {
        --m_queued;
    }
}

void LlmBudget::Record(Purpose purpose, ModelTier /*tier*/, LlmResponse const& response, bool fallbackUsed)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    ++m_stats.calls;
    if (!response.ok)
    {
        ++m_stats.errors;
    }
    if (fallbackUsed)
    {
        ++m_stats.fallbacks;
    }
    m_stats.tokensPrompt += response.promptTokens;
    m_stats.tokensCompletion += response.completionTokens;
    m_tokensThisHour += response.promptTokens + response.completionTokens;
    m_stats.latencyTotalMs += response.latencyMs;
    ++m_stats.latencyCount;

    uint8_t purposeIdx = static_cast<uint8_t>(purpose);
    if (purposeIdx < 8)
    {
        ++m_stats.byPurpose[purposeIdx];
    }
}

void LlmBudget::SetEnabled(bool enabled)
{
    m_enabled.store(enabled);
}

bool LlmBudget::IsEnabled() const
{
    return m_enabled.load();
}

BudgetStats LlmBudget::GetStats() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_stats;
}

uint32_t LlmBudget::TokensThisHour() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_tokensThisHour;
}

} // namespace LlmBots
