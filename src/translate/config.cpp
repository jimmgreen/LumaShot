#include "translate/engine.h"
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>

namespace lumashot::translate {
namespace {
std::string Trim(std::string_view text) {
    size_t a = 0, b = text.size();
    while (a < b && (text[a] == ' ' || text[a] == '\t' || text[a] == '\r')) ++a;
    while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t' || text[b - 1] == '\r')) --b;
    return std::string(text.substr(a, b - a));
}
// Values are single-line; strip anything that would break the format.
std::string Clean(std::string_view text) {
    std::string out;
    for (const char c : text) if (c != '\r' && c != '\n') out.push_back(c);
    return Trim(out);
}
}

std::filesystem::path DefaultConfigPath() {
    // Explicit override for isolated visual checks; never set in normal use.
    wchar_t custom[MAX_PATH]{};
    if (const DWORD n = GetEnvironmentVariableW(L"LUMASHOT_TRANSLATION_CONFIG", custom, MAX_PATH); n > 0 && n < MAX_PATH) return custom;
    PWSTR raw{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) return {};
    std::filesystem::path path(raw);
    CoTaskMemFree(raw);
    return path / L"LumaShot" / L"translation.ini";
}

Config LoadConfig(const std::filesystem::path& path) {
    Config config;
    if (path.empty()) return config;
    std::ifstream file(path, std::ios::binary);
    if (!file) return config;
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string text = buffer.str();
    if (text.rfind("\xef\xbb\xbf", 0) == 0) text.erase(0, 3);
    std::string section;
    size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        const auto line = Trim(std::string_view(text).substr(start, end - start));
        start = end + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') { section = line.substr(1, line.size() - 2); continue; }
        const auto equal = line.find('=');
        if (equal == std::string::npos) continue;
        const auto key = Trim(std::string_view(line).substr(0, equal));
        const auto value = Trim(std::string_view(line).substr(equal + 1));
        if (section == "Translation") {
            if (key == "Provider") config.provider = FindPreset(value) ? value : std::string{};
            else if (key == "Target") config.target = LanguageFromKey(value);
        } else if (section.rfind("Provider.", 0) == 0) {
            const auto id = section.substr(9);
            if (!FindPreset(id)) continue;
            auto& s = config.Edit(id);
            if (key == "Endpoint") s.endpoint = value;
            else if (key == "Model") s.model = value;
            else if (key == "AppId") s.app_id = value;
            else if (key == "Region") s.region = value;
            else if (key == "Key") s.protected_key = value;
        }
    }
    return config;
}

bool SaveConfig(const std::filesystem::path& path, const Config& config) {
    if (path.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::string text = "; LumaShot screenshot translation. Keys are encrypted with Windows DPAPI for this user.\r\n[Translation]\r\n";
    text += "Provider=" + Clean(config.provider) + "\r\n";
    text += "Target=" + std::string(LanguageKey(config.target)) + "\r\n";
    for (const auto& [id, s] : config.providers) {
        if (!FindPreset(id)) continue;
        if (s.endpoint.empty() && s.model.empty() && s.app_id.empty() && s.region.empty() && s.protected_key.empty()) continue;
        text += "\r\n[Provider." + id + "]\r\n";
        if (!s.endpoint.empty()) text += "Endpoint=" + Clean(s.endpoint) + "\r\n";
        if (!s.model.empty()) text += "Model=" + Clean(s.model) + "\r\n";
        if (!s.app_id.empty()) text += "AppId=" + Clean(s.app_id) + "\r\n";
        if (!s.region.empty()) text += "Region=" + Clean(s.region) + "\r\n";
        if (!s.protected_key.empty()) text += "Key=" + Clean(s.protected_key) + "\r\n";
    }
    auto temp = path;
    temp += L".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!file) return false;
    }
    return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
}
