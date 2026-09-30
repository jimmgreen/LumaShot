#pragma once
#include <chrono>
#include <cstdint>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// One-shot WinHTTP client for translation providers. HTTPS is required except
// for loopback/private-network hosts (local Ollama, LM Studio, vLLM), which
// bypass any proxy. Redirects are never followed, so credentials cannot leak to
// a redirected host. Cancellation closes the request from the stop callback.
namespace lumashot::translate::http {
struct Request {
    std::string method{"POST"};
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};

enum class Status { Ok, BadUrl, Insecure, Network, Timeout, Canceled, TooLarge };

struct Response {
    Status status{Status::Network};
    unsigned long error{};
    unsigned http_status{};
    std::string body;
    bool Success() const { return status == Status::Ok && http_status >= 200 && http_status < 300; }
};

struct Limits {
    std::chrono::milliseconds connect{std::chrono::seconds(6)};
    std::chrono::milliseconds receive{std::chrono::seconds(90)};
    size_t max_bytes{4u << 20};
};

// Hosts that may use plain http: localhost, 127/8, ::1, 10/8, 172.16/12, 192.168/16, 169.254/16.
bool PrivateHost(std::wstring_view host);
Response Send(const Request& request, std::stop_token stop, const Limits& limits = {});
}
