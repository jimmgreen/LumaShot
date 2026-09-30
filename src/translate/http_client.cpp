#include "translate/http_client.h"
#include "translate/json.h"
#include "lumashot_version.h"
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <cwctype>
#include <functional>
#include <mutex>
#include <optional>

namespace lumashot::translate::http {
namespace {
struct Internet {
    HINTERNET value{};
    ~Internet() { if (value) WinHttpCloseHandle(value); }
    Internet() = default;
    Internet(const Internet&) = delete;
    Internet& operator=(const Internet&) = delete;
};

int Milliseconds(std::chrono::milliseconds value) { return static_cast<int>(std::clamp<long long>(value.count(), 1, 0x7fffffff)); }

bool Octets(std::wstring_view host, int (&parts)[4]) {
    int index = 0, value = -1;
    for (const wchar_t c : host) {
        if (c >= L'0' && c <= L'9') { value = (value < 0 ? 0 : value) * 10 + (c - L'0'); if (value > 255) return false; }
        else if (c == L'.' && value >= 0 && index < 3) { parts[index++] = value; value = -1; }
        else return false;
    }
    if (index != 3 || value < 0) return false;
    parts[3] = value;
    return true;
}
}

bool PrivateHost(std::wstring_view host) {
    std::wstring lower(host);
    for (auto& c : lower) c = static_cast<wchar_t>(std::towlower(c));
    if (lower == L"localhost" || lower == L"::1" || lower == L"[::1]") return true;
    int o[4]{};
    if (!Octets(lower, o)) return false;
    return o[0] == 127 || o[0] == 10 || (o[0] == 172 && o[1] >= 16 && o[1] <= 31) || (o[0] == 192 && o[1] == 168) || (o[0] == 169 && o[1] == 254);
}

Response Send(const Request& request, std::stop_token stop, const Limits& limits) {
    Response result;
    if (stop.stop_requested()) { result.status = Status::Canceled; return result; }
    const auto wide = json::Utf16(request.url);
    URL_COMPONENTS parts{sizeof(parts)};
    wchar_t host[256]{}, path[4096]{}, extra[4096]{};
    parts.lpszHostName = host; parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path; parts.dwUrlPathLength = 4096;
    parts.lpszExtraInfo = extra; parts.dwExtraInfoLength = 4096;
    if (wide.empty() || !WinHttpCrackUrl(wide.c_str(), 0, 0, &parts) || !host[0]) { result.status = Status::BadUrl; return result; }
    const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    const bool local = PrivateHost(host);
    if (parts.nScheme != INTERNET_SCHEME_HTTPS && parts.nScheme != INTERNET_SCHEME_HTTP) { result.status = Status::BadUrl; return result; }
    if (!secure && !local) { result.status = Status::Insecure; return result; }
    std::wstring object = std::wstring(path) + extra;
    if (object.empty()) object = L"/";

    const std::wstring agent = L"LumaShot/" LUMASHOT_VERSION_WTEXT;
    Internet session, connection, handle;
    if (local) session.value = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    else {
        session.value = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session.value) session.value = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!session.value) { result.error = GetLastError(); return result; }
    const int connect = Milliseconds(limits.connect), receive = Milliseconds(limits.receive);
    WinHttpSetTimeouts(session.value, connect, connect, receive, receive);
    connection.value = WinHttpConnect(session.value, host, parts.nPort, 0);
    if (!connection.value) { result.error = GetLastError(); return result; }
    const auto method = json::Utf16(request.method);
    handle.value = WinHttpOpenRequest(connection.value, method.c_str(), object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    if (!handle.value) { result.error = GetLastError(); return result; }
    DWORD disable = WINHTTP_DISABLE_REDIRECTS;
    WinHttpSetOption(handle.value, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable));

    std::mutex guard;
    bool closed = false;
    const auto canceled = [&] { std::lock_guard lock(guard); return closed; };
    std::optional<std::stop_callback<std::function<void()>>> on_stop;
    on_stop.emplace(stop, std::function<void()>([&] {
        std::lock_guard lock(guard);
        if (!closed) { closed = true; WinHttpCloseHandle(handle.value); }
    }));
    const auto finish = [&](Status status) {
        on_stop.reset();
        if (closed) { handle.value = nullptr; status = Status::Canceled; }
        result.status = status;
        return result;
    };
    const auto network = [&] {
        result.error = GetLastError();
        return finish(result.error == ERROR_WINHTTP_TIMEOUT ? Status::Timeout : Status::Network);
    };

    std::wstring headers;
    for (const auto& [name, value] : request.headers) headers += json::Utf16(name) + L": " + json::Utf16(value) + L"\r\n";
    auto* body = request.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(request.body.data());
    const auto length = static_cast<DWORD>(request.body.size());
    if (canceled() || !WinHttpSendRequest(handle.value, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(), headers.empty() ? 0 : static_cast<DWORD>(-1L), body, length, length, 0)) return network();
    if (canceled() || !WinHttpReceiveResponse(handle.value, nullptr)) return network();
    DWORD status_code = 0, size = sizeof(status_code);
    if (canceled() || !WinHttpQueryHeaders(handle.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &size, WINHTTP_NO_HEADER_INDEX)) return network();
    result.http_status = status_code;
    std::vector<char> buffer(32 * 1024);
    for (;;) {
        DWORD read = 0;
        if (canceled() || !WinHttpReadData(handle.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) return network();
        if (read == 0) break;
        if (result.body.size() + read > limits.max_bytes) return finish(Status::TooLarge);
        result.body.append(buffer.data(), read);
    }
    return finish(Status::Ok);
}
}
