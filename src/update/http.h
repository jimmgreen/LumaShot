#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

// Minimal synchronous WinHTTP GET for the updater worker threads.
// HTTPS only (plain http is accepted solely for 127.0.0.1 when a test opts in),
// honors the system/WPAD proxy, follows redirects (never https -> http), caps the
// body size, and aborts promptly when the stop token fires.
namespace lumashot::update::http {
struct Options {
    std::chrono::milliseconds connect_timeout{8000};
    std::chrono::milliseconds receive_timeout{20000};
    // Abort when fewer than min_bytes_per_second arrive over any throughput window
    // (0 disables). Lets the downloader leave a crawling proxy for the next one.
    std::uint64_t min_bytes_per_second{};
    std::chrono::milliseconds throughput_window{20000};
    bool allow_loopback_http{};
    // Resume: request bytes from this offset. A 206 reply sets Result::partial;
    // a server that ignores Range answers 200 and the body starts at zero.
    std::uint64_t range_start{};
    // Called once after the headers, before any body bytes: (partial, full size or 0).
    // Returning false aborts with Status::Sink.
    std::function<bool(bool, std::uint64_t)> on_response;
};
enum class Status { Ok, Canceled, BadUrl, Network, HttpStatus, TooLarge, Slow, Sink };
struct Result {
    Status status{Status::Network};
    unsigned long error{};        // Win32/WinHTTP error for Network
    unsigned long http_status{};  // for HttpStatus
    std::uint64_t bytes{};
    bool partial{};               // body continues at Options::range_start
    std::uint64_t total{};        // full resource size when known (Content-Length / Content-Range)
};
// sink returns false to abort (Status::Sink). progress receives (received, content-length or 0).
Result Get(const std::string& url, std::stop_token stop, std::uint64_t max_bytes,
    const std::function<bool(std::span<const std::uint8_t>)>& sink,
    const std::function<void(std::uint64_t, std::uint64_t)>& progress = {}, const Options& options = {});
Result GetBytes(const std::string& url, std::stop_token stop, std::uint64_t max_bytes, std::vector<std::uint8_t>& out, const Options& options = {});
std::wstring UserAgent();
}