/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 *
 * Thread-safe request broker. Owns the provider, the worker pool, the request
 * queue and the completion queue.
 *
 *   [caller thread]  Submit(request, callback)   - budget check, enqueue, return
 *   [worker threads] dequeue, provider->Complete (blocking), retry, validate
 *   [world thread]   DrainCompletions()          - apply results via callbacks
 *
 * Request objects must never carry game pointers; use ObjectGuid / snapshots.
 */

#ifndef LLM_CLIENT_H
#define LLM_CLIENT_H

#include "LlmProvider.h"

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace LlmBots
{

class LlmBudget;

struct LlmCompletion
{
    LlmRequest  request;
    LlmResponse response;
    std::function<void(LlmResponse const&)> callback; // run on world thread
};

class LlmClient
{
public:
    explicit LlmClient(std::unique_ptr<LlmProvider> provider);
    ~LlmClient();

    // Immovable: worker threads hold references into it.
    LlmClient(LlmClient const&) = delete;
    LlmClient& operator=(LlmClient const&) = delete;

    // Starts the worker threads. Call once after configuration.
    void Start(uint32_t workerCount, LlmBudget* budget);

    // Stops workers and joins them. Safe from any thread; workers may be
    // blocked in an HTTP call, so this can take up to TimeoutMs.
    void Stop();

    // Budget-checked submit. Returns false if refused (caller must degrade).
    // The callback runs later, on the world thread, via DrainCompletions().
    bool Submit(LlmRequest request, std::function<void(LlmResponse const&)> callback);

    // Continue a request whose response came back with needsTools set, now
    // that the tools have been run. World thread only - the results are read
    // out of game state.
    //
    // Deliberately skips the call-rate check: the tokens for the earlier
    // rounds are already spent, so dropping the conversation half way wastes
    // them and leaves the bot silent. Usage is still recorded per round, and
    // maxRounds caps the worst case.
    bool Resume(LlmRequest request, LlmResponse const& response,
        std::vector<ToolResult> results, std::function<void(LlmResponse const&)> callback);

    // Moves all completed requests out. Called from the world thread only.
    void DrainCompletions(std::vector<LlmCompletion>& out);

    // True while the circuit breaker is refusing submits.
    bool IsCircuitOpen() const;

    std::string ProviderName() const;

private:
    struct PendingRequest
    {
        LlmRequest  request;
        std::function<void(LlmResponse const&)> callback;
    };

    void WorkerLoop();
    void RecordFailure(ErrorKind kind);

    std::unique_ptr<LlmProvider> m_provider;
    LlmBudget* m_budget = nullptr;

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<PendingRequest> m_queue;
    std::vector<LlmCompletion> m_completions;

    std::vector<std::thread> m_workers;
    bool m_stopping = false;

    // Circuit breaker: after this many consecutive transient failures the
    // provider parks for m_circuitCooldownSec, refusing submits meanwhile.
    std::atomic<uint32_t> m_consecutiveFailures = 0;
    uint32_t const m_circuitThreshold = 3;
    std::atomic<bool> m_circuitOpen = false;
    std::chrono::steady_clock::time_point m_circuitOpenUntil{};
    uint32_t const m_circuitCooldownSec = 30;
};

} // namespace LlmBots

#endif // LLM_CLIENT_H
