/*
 * This file is part of the LLM bots feature of mod-playerbots (AzerothCore).
 *
 * Minimal synchronous HTTPS client over Boost.Beast + OpenSSL. Blocking:
 * only safe on the module's worker threads. No WoW dependencies.
 */

#ifndef HTTPS_CLIENT_H
#define HTTPS_CLIENT_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace LlmBots
{

struct HttpResponse
{
    bool        ok = false;
    int         status = 0;
    std::string body;
    std::string error;
};

// POSTs a JSON body to https://host[:port]/path with the given headers
// (e.g. "api-key", "Content-Type"). TLS verification is on.
HttpResponse HttpsPostJson(std::string const& host, std::string const& port,
    std::string const& path, std::string const& body,
    std::vector<std::pair<std::string, std::string>> const& headers,
    uint32_t timeoutMs);

// Splits "https://host[:port]" and an absolute path into its parts.
// Returns false on malformed input.
bool SplitUrl(std::string const& url, std::string& host, std::string& port, std::string& path);

} // namespace LlmBots

#endif // HTTPS_CLIENT_H
