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
 */

#include "HttpsClient.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>

#include <vector>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = net::ip::tcp;

namespace LlmBots
{

bool SplitUrl(std::string const& url, std::string& host, std::string& port, std::string& path)
{
    std::string rest = url;
    if (rest.rfind("https://", 0) == 0)
    {
        rest = rest.substr(8);
        port = "443";
    }
    else if (rest.rfind("http://", 0) == 0)
    {
        rest = rest.substr(7);
        port = "80";
    }
    else
    {
        return false;
    }

    size_t const slash = rest.find('/');
    std::string authority = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    path = (slash == std::string::npos) ? "/" : rest.substr(slash);

    size_t const colon = authority.rfind(':');
    if (colon != std::string::npos)
    {
        host = authority.substr(0, colon);
        port = authority.substr(colon + 1);
    }
    else
    {
        host = authority;
    }

    return !host.empty();
}

HttpResponse HttpsPostJson(std::string const& host, std::string const& port,
    std::string const& path, std::string const& body,
    std::vector<std::pair<std::string, std::string>> const& headers,
    uint32_t timeoutMs)
{
    HttpResponse result;

    try
    {
        net::io_context ioc;
        ssl::context ctx(ssl::context::tlsv12_client);
        ctx.set_default_verify_paths();
        ctx.load_verify_file("ca-bundle.crt");
        ctx.set_verify_mode(ssl::verify_peer);

        tcp::resolver resolver(ioc);
        beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);

        // Hostname verification must be configured before the TLS handshake.
        stream.set_verify_callback(ssl::host_name_verification(host));

        if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()))
        {
            result.error = "failed to set SNI hostname";
            return result;
        }

        auto const results = resolver.resolve(host, port);
        beast::get_lowest_layer(stream).expires_after(std::chrono::milliseconds(timeoutMs));
        beast::get_lowest_layer(stream).connect(results);
        stream.handshake(ssl::stream_base::client);

        http::request<http::string_body> req(http::verb::post, path, 11);
        req.set(http::field::host, host);
        req.set(http::field::content_type, "application/json");
        req.set(http::field::user_agent, "mod-playerbots-llm/1.0");
        for (auto const& [key, value] : headers)
        {
            req.set(key, value);
        }
        req.body() = body;
        req.prepare_payload();

        beast::get_lowest_layer(stream).expires_after(std::chrono::milliseconds(timeoutMs));
        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        beast::error_code ec;
        stream.shutdown(ec); // ec ignored: server may half-close first

        result.ok = true;
        result.status = res.result_int();
        result.body = res.body();
    }
    catch (std::exception const& e)
    {
        result.ok = false;
        result.error = e.what();
    }

    return result;
}

} // namespace LlmBots
