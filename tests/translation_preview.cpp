// On-demand visual check (not a ctest): renders a synthetic English UI, bakes
// Chinese translations into it with the production renderer and captures the
// production translation panel off-screen in light and dark themes.
//   cmake --build build --target lumashot_translation_preview
//   build\lumashot_translation_preview.exe   → build\translation-*.png
#include "pin/translation.h"
#include "pin/translation_panel.h"
#include "export/png.h"
#include "translate/engine.h"
#include "translate/offline.h"
#include <winioctl.h>
#include <filesystem>
#include <fstream>
#include "ui/memory_target.h"
#include <windows.h>
#include <objbase.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <iostream>
#include <stdexcept>

using namespace lumashot;
using Microsoft::WRL::ComPtr;

namespace {
struct Item { std::wstring text; D2D1_RECT_F rect; float size; uint32_t ink; bool bold; };

Frame Synthetic(std::vector<translate::Block>& blocks, std::vector<std::wstring>& sources) {
    Frame f;
    f.bounds = {0, 0, 720, 380};
    f.pixels.assign(size_t(720) * 380, 0xfff7f8fa);
    ComPtr<ID2D1Factory> factory;
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf());
    auto rt = CreateMemoryRenderTarget(factory.Get(), f.pixels.data(), 720, 380, 720);
    ComPtr<IDWriteFactory> dw;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dw.GetAddressOf()));
    ComPtr<ID2D1SolidColorBrush> brush;
    rt->CreateSolidColorBrush(D2D1::ColorF(0), brush.GetAddressOf());
    rt->BeginDraw();
    brush->SetColor(D2D1::ColorF(0x1f4e8c));
    rt->FillRectangle({0, 0, 720, 64}, brush.Get());
    brush->SetColor(D2D1::ColorF(0x2f7de1));
    rt->FillRoundedRectangle(D2D1::RoundedRect({520, 300, 690, 348}, 8, 8), brush.Get());
    const std::vector<Item> items{
        {L"Account settings", {28, 16, 520, 48}, 24, 0xffffff, true},
        {L"Choose how often we send you updates about new features.", {28, 96, 692, 122}, 17, 0x2b2f36, false},
        {L"Your data is stored securely and never shared with third parties.", {28, 138, 692, 164}, 17, 0x2b2f36, false},
        {L"Two-factor authentication is recommended for all accounts.", {28, 196, 692, 222}, 17, 0x6b7280, false},
        {L"Save changes", {540, 312, 670, 336}, 17, 0xffffff, true},
    };
    for (const auto& item : items) {
        ComPtr<IDWriteTextFormat> format;
        dw->CreateTextFormat(L"Segoe UI", nullptr, item.bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, item.size, L"en-US", format.GetAddressOf());
        ComPtr<IDWriteTextLayout> layout;
        dw->CreateTextLayout(item.text.c_str(), UINT32(item.text.size()), format.Get(), item.rect.right - item.rect.left, 200, layout.GetAddressOf());
        DWRITE_TEXT_METRICS m{};
        layout->GetMetrics(&m);
        brush->SetColor(D2D1::ColorF(item.ink));
        rt->DrawTextLayout({item.rect.left, item.rect.top}, layout.Get(), brush.Get());
        translate::Block block;
        block.box = {item.rect.left + m.left, item.rect.top + m.top, item.rect.left + m.left + m.width, item.rect.top + m.top + m.height};
        block.line_height = m.height;
        block.text = item.text;
        blocks.push_back(block);
        sources.push_back(item.text);
    }
    if (FAILED(rt->EndDraw())) throw std::runtime_error("synthetic render");
    for (auto& p : f.pixels) p |= 0xff000000u;
    return f;
}

void Pump(int ms) {
    const auto end = GetTickCount64() + ULONGLONG(ms);
    MSG msg{};
    while (GetTickCount64() < end) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        Sleep(10);
    }
}

bool Capture(HWND window, const wchar_t* path) {
    RECT r{};
    GetWindowRect(window, &r);
    const int w = r.right - r.left, h = r.bottom - r.top;
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    auto old = SelectObject(dc, dib);
    const bool ok = PrintWindow(window, dc, 2 /*PW_RENDERFULLCONTENT*/) != FALSE;
    Frame f;
    f.bounds = {0, 0, w, h};
    f.pixels.assign(static_cast<uint32_t*>(bits), static_cast<uint32_t*>(bits) + size_t(w) * h);
    for (auto& p : f.pixels) p |= 0xff000000u;
    SelectObject(dc, old);
    DeleteDC(dc);
    DeleteObject(dib);
    if (ok) SavePng(f, path);
    return ok;
}

struct Click { float x, y; };  // DIP, client coordinates
// Runs the settings window in its own process with an isolated config and
// offline folder, optionally clicks (posted, no real input) and captures it.
void Settings(bool dark, const translate::Config& config, const std::wstring& offline_root, const std::vector<Click>& clicks, const wchar_t* path) {
    wchar_t module[MAX_PATH]{};
    GetModuleFileNameW(nullptr, module, MAX_PATH);
    std::wstring folder(module);
    folder.resize(folder.find_last_of(L'\\'));
    const std::wstring ini = folder + L"\\translation-preview.ini";
    translate::SaveConfig(ini, config);
    SetEnvironmentVariableW(L"LUMASHOT_TRANSLATION_CONFIG", ini.c_str());
    SetEnvironmentVariableW(L"LUMASHOT_TRANSLATION_PREVIEW", L"1");
    SetEnvironmentVariableW(L"LUMASHOT_OFFLINE_ROOT", offline_root.c_str());
    std::wstring exe = folder + L"\\LumaShot.exe";
    std::wstring command = L"\"" + exe + L"\" --translation-settings" + (dark ? L" --dark" : L"");
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &info)) { std::cout << "settings launch failed\n"; return; }
    HWND settings = nullptr;
    for (int i = 0; i < 100 && !settings; ++i) { Sleep(50); settings = FindWindowW(L"LumaShot.TranslationSettings", nullptr); }
    Sleep(1500); // local-model and hardware probes, first paint
    if (settings) {
        const float scale = GetDpiForWindow(settings) / 96.f;
        for (const auto& click : clicks) {
            const LPARAM at = MAKELPARAM(int(click.x * scale), int(click.y * scale));
            PostMessageW(settings, WM_MOUSEMOVE, 0, at);
            PostMessageW(settings, WM_LBUTTONDOWN, MK_LBUTTON, at);
            PostMessageW(settings, WM_LBUTTONUP, 0, at);
            Sleep(250);
        }
        if (!clicks.empty()) PostMessageW(settings, WM_MOUSEMOVE, 0, MAKELPARAM(2, 2));
        Sleep(300);
        std::cout << (Capture(settings, path) ? "settings ok " : "settings capture failed ") << std::filesystem::path(path).filename().string() << '\n';
        PostMessageW(settings, WM_CLOSE, 0, 0);
    }
    WaitForSingleObject(info.hProcess, 5000);
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
}
// Installed state without downloading: sparse file of the exact size + markers.
void FakeInstall(const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    SetEnvironmentVariableW(L"LUMASHOT_OFFLINE_ROOT", root.c_str());
    const auto& model = *translate::offline::FindModel(translate::offline::DefaultModel);
    const auto file = translate::offline::ModelPath(model);
    fs::create_directories(file.parent_path());
    const HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD returned = 0;
        DeviceIoControl(h, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr);
        LARGE_INTEGER end{};
        end.QuadPart = LONGLONG(model.size);
        SetFilePointerEx(h, end, nullptr, FILE_BEGIN);
        SetEndOfFile(h);
        CloseHandle(h);
    }
    std::ofstream(file.native() + L".verified", std::ios::binary) << model.sha256;
    fs::create_directories(translate::offline::RuntimeDirectory());
    std::ofstream(translate::offline::ServerPath(), std::ios::binary) << "stub";
    std::ofstream(translate::offline::RuntimeDirectory() / L"ggml-base.dll", std::ios::binary) << "stub";
    std::ofstream(translate::offline::RuntimeDirectory() / L".verified", std::ios::binary) << translate::offline::RuntimePackage().build;
    // A paused 7B download shows the resume state.
    const auto& big = *translate::offline::FindModel("hy-mt1.5-7b");
    std::ofstream(translate::offline::ModelPath(big).native() + L".partial", std::ios::binary) << std::string(1 << 20, 'x');
}
}

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        std::vector<translate::Block> blocks;
        std::vector<std::wstring> sources;
        const auto image = Synthetic(blocks, sources);
        const std::vector<std::wstring> results{L"账户设置", L"选择我们向你发送新功能更新的频率。", L"你的数据会被安全存储，绝不会与第三方共享。", L"建议所有账户启用双重身份验证。", L"保存更改"};
        const auto translated = pin_translation::Render(image, blocks, results, translate::Language::ChineseSimplified);
        Frame both;
        both.bounds = {0, 0, 720, 380 * 2 + 12};
        both.pixels.assign(size_t(720) * (380 * 2 + 12), 0xffd0d4da);
        for (int y = 0; y < 380; ++y) {
            std::copy_n(image.pixels.begin() + ptrdiff_t(y) * 720, 720, both.pixels.begin() + ptrdiff_t(y) * 720);
            std::copy_n(translated.pixels.begin() + ptrdiff_t(y) * 720, 720, both.pixels.begin() + ptrdiff_t(y + 392) * 720);
        }
        SavePng(both, L"build\\translation-render.png");
        std::cout << "render ok\n";

        for (bool dark : {false, true}) {
            TranslationPanel panel(nullptr, {});
            TranslationView view;
            view.state = TranslationView::State::Done;
            view.engine = L"DeepSeek";
            view.source = translate::Language::English;
            view.target = translate::Language::ChineseSimplified;
            view.sources = sources;
            view.results = results;
            view.dark = dark;
            panel.Update(view);
            panel.Place({200, 200, 920, 580});
            panel.Show();
            // Park it off-screen: PrintWindow reads the DWM surface, so nothing appears on the desktop.
            SetWindowPos(panel.Window(), nullptr, -32000, -32000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            Pump(450);
            std::cout << (Capture(panel.Window(), dark ? L"build\\translation-panel-dark.png" : L"build\\translation-panel-light.png") ? "panel ok\n" : "panel capture failed\n");
            if (!dark) {
                view.state = TranslationView::State::NeedsSetup;
                view.error = L"还没有选择翻译引擎";
                view.sources.clear();
                view.results.clear();
                panel.Update(view);
                Pump(250);
                Capture(panel.Window(), L"build\\translation-panel-setup.png");
            }
        }
        // Settings window: its own process, isolated config and offline folder, parked off-screen.
        namespace fs = std::filesystem;
        const fs::path root = fs::absolute(L"build\\offline-preview");
        std::error_code error;
        fs::remove_all(root, error);
        translate::Config cloud;
        cloud.provider = "deepseek";
        cloud.Edit("deepseek").model = "deepseek-v4-flash";
        for (bool dark : {false, true}) Settings(dark, cloud, (root / L"none").wstring(), {}, dark ? L"build\\translation-settings-dark.png" : L"build\\translation-settings-light.png");
        Settings(false, cloud, (root / L"none").wstring(), {{300, 94}}, L"build\\translation-engine-popup.png");
        Settings(false, cloud, (root / L"none").wstring(), {{300, 285}}, L"build\\translation-language-popup.png");
        translate::Config local;
        local.provider = "offline";
        for (bool dark : {false, true}) Settings(dark, local, (root / L"empty").wstring(), {}, dark ? L"build\\translation-offline-dark.png" : L"build\\translation-offline-light.png");
        FakeInstall(root / L"installed");
        Settings(false, local, (root / L"installed").wstring(), {}, L"build\\translation-offline-installed.png");
        Settings(true, local, (root / L"installed").wstring(), {}, L"build\\translation-offline-installed-dark.png");
        Settings(false, local, (root / L"installed").wstring(), {{455, 127}}, L"build\\translation-offline-details.png");
        Settings(false, local, (root / L"installed").wstring(), {{300, 94}}, L"build\\translation-offline-engines.png");
        fs::remove_all(root, error);
    } catch (const std::exception& e) {
        std::cout << "error: " << e.what() << '\n';
        return 1;
    }
    CoUninitialize();
    return 0;
}
