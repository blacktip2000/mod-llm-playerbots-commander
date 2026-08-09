/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 */

#include "LlmClient.h"

#include "Log.h"
#include "LlmBudget.h"

#include <atomic>
#include <thread>

namespace LlmBots
{

namespace
{

// Jittered backoff between the first attempt and its retry. Small, because
// the rate ceiling is far more likely to bite than a lost request.
uint32_t BackoffMs(uint32_t attempt)
{
    uint32_t const base = attempt == 0 ? 250u : 800u;
    return base + static_cast<uint32_t>(attempt * 113u % 400u);
}

} // namespace

LlmClient::LlmClient(std::unique_ptr<LlmProvider> provider)
    : m_provider(std::move(provider))
{
}

LlmClient::~LlmClient()
{
    Stop();
}

void LlmClient::Start(uint32_t workerCount, LlmBudget* budget)
{
    m_budget = budget;
    for (uint32_t i = 0; i < workerCount; ++i)
    {
        m_workers.emplace_back([this] { WorkerLoop(); });
    }
}

void LlmClient::Stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping)
        {
            return;
        }
        m_stopping = true;
        m_queue.clear();
    }
    m_cv.notify_all();
    for (auto& worker : m_workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
    m_workers.clear();
}

bool LlmClient::Submit(LlmRequest request, std::function<void(LlmResponse const&)> callback)
{
    if (!m_budget)
    {
        return false;
    }

    BudgetResult const result = m_budget->Check(request.purpose, request.tier);
    if (result != BudgetResult::Allowed)
    {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping)
        {
            return false;
        }
        if (m_circuitOpen && std::chrono::steady_clock::now() < m_circuitOpenUntil)
        {
            return false;
        }
        m_queue.emplace_back(PendingRequest{ std::move(request), std::move(callback) });
    }
    m_budget->OnQueued();
    m_cv.notify_one();
    return true;
}

bool LlmClient::Resume(LlmRequest request, LlmResponse const& response,
    std::vector<ToolResult> results, std::function<void(LlmResponse const&)> callback)
{
    request.transcript = response.transcript;
    request.toolResults = std::move(results);
    ++request.toolRound;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping)
        {
            return false;
        }
        if (m_circuitOpen && std::chrono::steady_clock::now() < m_circuitOpenUntil)
        {
            return false;
        }
        m_queue.emplace_back(PendingRequest{ std::move(request), std::move(callback) });
    }
    if (m_budget)
    {
        m_budget->OnQueued();
    }
    m_cv.notify_one();
    return true;
}

void LlmClient::DrainCompletions(std::vector<LlmCompletion>& out)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_completions.empty())
    {
        out.insert(out.end(), m_completions.begin(), m_completions.end());
        m_completions.clear();
    }
}

bool LlmClient::IsCircuitOpen() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_circuitOpen.load() && std::chrono::steady_clock::now() < m_circuitOpenUntil;
}

std::string LlmClient::ProviderName() const
{
    return m_provider ? m_provider->Name() : "<none>";
}

void LlmClient::WorkerLoop()
{
    for (;;)
    {
        PendingRequest pending;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_stopping || !m_queue.empty(); });
            if (m_stopping)
            {
                return;
            }
            pending = std::move(m_queue.front());
            m_queue.erase(m_queue.begin());
        }
        if (m_budget)
        {
            m_budget->OnDequeued();
        }

        LlmResponse response = m_provider->Complete(pending.request);

        // One retry on transient failures (429/5xx/network), jittered.
        if (!response.ok && response.errorKind == ErrorKind::Transient && pending.request.purpose != Purpose::Test)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(BackoffMs(0)));
            response = m_provider->Complete(pending.request);
        }

        if (!response.ok)
        {
            RecordFailure(response.errorKind);
            LOG_WARN("llmbots.provider", "LLM call failed ({}): {}", pending.request.schemaName, response.error);
        }
        else
        {
            m_consecutiveFailures = 0;
            m_circuitOpen = false;
        }

        if (m_budget)
        {
            m_budget->Record(pending.request.purpose, pending.request.tier, response, !response.ok);
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_stopping)
            {
                m_completions.emplace_back(LlmCompletion{ pending.request, std::move(response), std::move(pending.callback) });
            }
        }
    }
}

void LlmClient::RecordFailure(ErrorKind kind)
{
    if (kind != ErrorKind::Transient)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (++m_consecutiveFailures >= m_circuitThreshold)
    {
        m_circuitOpen = true;
        m_circuitOpenUntil = std::chrono::steady_clock::now() + std::chrono::seconds(m_circuitCooldownSec);
    }
}

} // namespace LlmBots
