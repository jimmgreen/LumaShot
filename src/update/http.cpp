#include "update/http.h"
#include "lumashot_version.h"
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <mutex>
#include <optional>

namespace lumashot::update::http {
namespace {
struct Internet {
    HINTERNET value{};
    ~Internet() { if (value) WinHttpCloseHandle(value); }
    Internet() = default;
    Internet(const Internet&) = delete;
    Internet& operator=(const Internet&) = delete;
};

std::wstring Wide(const std::string& text) {
    std::wstring out;
    out.reserve(text.size());
    for (const char c : text) {
        if (static_cast<unsigned char>(c) < 0x21 || static_cast<unsigned char>(c) > 0x7e) return {};
        out.push_back(static_cast<wchar_t>(c));
    }
    return out;
}

int Milliseconds(std::chrono::milliseconds value) {
    return static_cast<int>(std::min<long long>(value.count(), 0x7fffffff));
}
}

std::wstring UserAgent() { return L"LumaShot/" LUMASHOT_VERSION_WTEXT L" (+https://github.com/jimmgreen/LumaShot)"; }

Result Get(const std::string& url, std::stop_token stop, std::uint64_t max_bytes,
    const std::function<bool(std::span<const std::uint8_t>)>& sink,
    const std::function<void(std::uint64_t, std::uint64_t)>& progress, const Options& options) {
    Result result;
    if (stop.stop_requested()) { result.status = Status::Canceled; return result; }
    const auto wide = Wide(url);
    URL_COMPONENTS parts{sizeof(parts)};
    wchar_t host[256]{}, path[2048]{};
    parts.lpszHostName = host; parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path; parts.dwUrlPathLength = 2048;
    wchar_t extra[1024]{};
    parts.lpszExtraInfo = extra; parts.dwExtraInfoLength = 1024;
    if (wide.empty() || !WinHttpCrackUrl(wide.c_str(), 0, 0, &parts)) { result.status = Status::BadUrl; return result; }
    const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    const bool loopback = parts.nScheme == INTERNET_SCHEME_HTTP && options.allow_loopback_http && std::wstring_view(host) == L"127.0.0.1";
    if (!secure && !loopback) { result.status = Status::BadUrl; return result; }
    const std::wstring object = std::wstring(path) + extra;

    Internet session, connection, request;
    session.value = WinHttpOpen(UserAgent().c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.value) session.value = WinHttpOpen(UserAgent().c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.value) { result.error = GetLastError(); return result; }
    const int connect = Milliseconds(options.connect_timeout), receive = Milliseconds(options.receive_timeout);
    WinHttpSetTimeouts(session.value, connect, connect, receive, receive);
    connection.value = WinHttpConnect(session.value, host, parts.nPort, 0);
    if (!connection.value) { result.error = GetLastError(); return result; }
    request.value = WinHttpOpenRequest(connection.value, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request.value) { result.error = GetLastError(); return result; }
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));

    // Closing the request from the stop callback makes the blocking call return
    // ERROR_WINHTTP_OPERATION_CANCELLED; the owner then never touches it again.
    std::mutex guard;
    bool closed = false;
    const auto canceled = [&] { std::lock_guard lock(guard); return closed; };
    std::optional<std::stop_callback<std::function<void()>>> on_stop;
    on_stop.emplace(stop, std::function<void()>([&] {
        std::lock_guard lock(guard);
        if (!closed) { closed = true; WinHttpCloseHandle(request.value); }
    }));
    const auto finish = [&](Status status) {
        on_stop.reset();
        if (closed) { request.value = nullptr; status = Status::Canceled; }
        result.status = status;
        return result;
    };
    const auto network = [&] { result.error = GetLastError(); return finish(Status::Network); };

    std::wstring headers = L"Cache-Control: no-cache\r\nPragma: no-cache\r\n";
    if (options.range_start) headers += L"Range: bytes=" + std::to_wstring(options.range_start) + L"-\r\n";
    if (canceled() || !WinHttpSendRequest(request.value, headers.c_str(), static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) return network();
    if (canceled() || !WinHttpReceiveResponse(request.value, nullptr)) return network();
    DWORD status_code = 0, length = sizeof(status_code);
    if (canceled() || !WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &length, WINHTTP_NO_HEADER_INDEX)) return network();
    const bool partial = status_code == 206 && options.range_start;
    if (status_code != 200 && !partial) { result.http_status = status_code; return finish(Status::HttpStatus); }
    std::uint64_t total = 0;
    {
        wchar_t text[32]{};
        DWORD size = sizeof(text);
        if (WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, text, &size, WINHTTP_NO_HEADER_INDEX)) total = _wcstoui64(text, nullptr, 10);
    }
    if (partial) {
        // Content-Range: bytes <start>-<end>/<size>; the start must match the request.
        wchar_t text[96]{};
        DWORD size = sizeof(text);
        if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CONTENT_RANGE, WINHTTP_HEADER_NAME_BY_INDEX, text, &size, WINHTTP_NO_HEADER_INDEX)) { result.error = ERROR_WINHTTP_INVALID_SERVER_RESPONSE; return finish(Status::Network); }
        const std::wstring_view range(text);
        const auto space = range.find(L' '), slash = range.find(L'/');
        if (space == std::wstring_view::npos || _wcstoui64(text + space + 1, nullptr, 10) != options.range_start) { result.error = ERROR_WINHTTP_INVALID_SERVER_RESPONSE; return finish(Status::Network); }
        if (slash != std::wstring_view::npos && range[slash + 1] != L'*') result.total = _wcstoui64(text + slash + 1, nullptr, 10);
        result.partial = true;
    } else result.total = total;
    if (options.on_response && !options.on_response(result.partial, result.total)) return finish(Status::Sink);
    if (total > max_bytes) return finish(Status::TooLarge);

    std::vector<std::uint8_t> buffer(64 * 1024);
    auto window_start = std::chrono::steady_clock::now();
    std::uint64_t window_bytes = 0;
    for (;;) {
        DWORD read = 0;
        if (canceled() || !WinHttpReadData(request.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) return network();
        if (read == 0) break;
        result.bytes += read;
        if (result.bytes > max_bytes) return finish(Status::TooLarge);
        if (!sink(std::span<const std::uint8_t>(buffer.data(), read))) return finish(Status::Sink);
        if (progress) progress(result.bytes, total);
        window_bytes += read;
        const auto now = std::chrono::steady_clock::now();
        if (options.min_bytes_per_second && now - window_start >= options.throughput_window) {
            const auto seconds = std::chrono::duration<double>(now - window_start).count();
            if (static_cast<double>(window_bytes) < static_cast<double>(options.min_bytes_per_second) * seconds) return finish(Status::Slow);
            window_start = now;
            window_bytes = 0;
        }
    }
    if (total && result.bytes != total) { result.error = ERROR_WINHTTP_INVALID_SERVER_RESPONSE; return finish(Status::Network); }
    return finish(Status::Ok);
}

Result GetBytes(const std::string& url, std::stop_token stop, std::uint64_t max_bytes, std::vector<std::uint8_t>& out, const Options& options) {
    out.clear();
    return Get(url, std::move(stop), max_bytes, [&](std::span<const std::uint8_t> chunk) { out.insert(out.end(), chunk.begin(), chunk.end()); return true; }, {}, options);
}
}