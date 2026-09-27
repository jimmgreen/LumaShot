#include <lumatext/lumatext.hpp>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wincodec.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace {
constexpr wchar_t sample[] = L"微软雅黑：清晰的文字，安静的阅读。Aa Bb 0123456789";
enum { Theme = 101, Size, Dpi, Filter, Gamma, Regular, Bold, Phase, Reset, Text, Blend };
struct App {
  ComPtr<ID2D1Factory> factory;
  ComPtr<IDWriteFactory> write;
  ComPtr<ID2D1HwndRenderTarget> windowTarget;
  ComPtr<IDWriteTextFormat> label;
  LumaText::Context context;
  LumaText::FontFace regular, bold;
  LumaText::FontCascade cascade;
  LumaText::RenderProfile profile;
  LumaText::Renderer renderer;
  std::array<LumaText::TextLayout, 8> layouts;
  std::array<HWND, 11> controls{};
  std::vector<HWND> captions;
  HFONT controlFont = nullptr;
  bool dark = false;
  float size = 16, dpi = 96, gamma = .9f, regularWeight = .02f, boldWeight = 0;
  int filter = LT_RASTER_FILTER_MITCHELL, phase = 0;
  float layoutWidth = -1;
  std::wstring text = sample;
  bool dirty = true, updating = false;
  bool blend = true;
  int scroll = 0;
  ~App() { if (controlFont) DeleteObject(controlFont); }
} app;

lt_render_config config(bool candidate) {
  auto c = LumaText::Descriptor<lt_render_config>();
  c.coverage_gamma = candidate ? app.gamma : .85f;
  c.coverage_contrast = 1;
  c.raster_filter = static_cast<uint8_t>(app.filter);
  c.flags = candidate && app.blend ? LT_RENDER_CONFIG_LINEAR_BLEND | LT_RENDER_CONFIG_KNOWN_BACKGROUND : 0;
  return c;
}
bool rebuild(float width) {
  if (!app.dirty && app.layoutWidth == width) return true;
  app.profile.reset();
  auto p = LumaText::Descriptor<lt_render_profile_desc>();
  p.light = p.dark = config(true);
  p.regular_optical_weight = app.regularWeight;
  p.bold_optical_weight = app.boldWeight;
  if (lt_render_profile_create(&p, app.profile.put()) != LT_OK) return false;
  const std::array<std::wstring, 4> lines = {
    app.text, L"永 国 重 最 美 微 软 雅 黑，标点。、：；！？",
    L"Hamburgefontsiv  Il1 O0  AV To  fi fl  0123456789",
    L"阅读与设计 / Windows / macOS / 细节决定体验"};
  for (size_t i = 0; i < app.layouts.size(); ++i) {
    app.layouts[i].reset();
    auto s = LumaText::Descriptor<lt_text_style>();
    s.cascade = app.cascade.get(); s.font_size = app.size; s.weight = i < 4 ? 400 : 700;
    auto d = LumaText::Descriptor<lt_text_layout_desc>();
    d.text = lines[i % 4].c_str(); d.text_length = static_cast<uint32_t>(lines[i % 4].size());
    d.base_style = s; d.locale = "zh-CN"; d.max_width = width;
    d.ellipsis = LT_TEXT_ELLIPSIS_END;
    if (lt_text_layout_create(app.context.get(), &d, app.layouts[i].put()) != LT_OK) return false;
  }
  app.layoutWidth = width; app.dirty = false;
  return true;
}
bool attach(ID2D1RenderTarget* target) {
  app.renderer.reset();
  auto d = LumaText::Descriptor<lt_d2d_desc>();
  d.render_target = target; d.manage_begin_end_draw = false;
  return lt_d2d_renderer_create(app.context.get(), &d, app.renderer.put()) == LT_OK;
}
void label(ID2D1RenderTarget* t, ID2D1SolidColorBrush* b, const std::wstring& s,
           float x, float y, float w, float h = 30) {
  t->DrawTextW(s.c_str(), static_cast<UINT32>(s.size()), app.label.Get(),
    D2D1::RectF(x, y, x + w, y + h), b, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
bool panel(ID2D1RenderTarget* t, float top) {
  const auto dimensions = t->GetSize();
  const float width = (dimensions.width - 48) / 2;
  if (width < 50 || !rebuild(width - 28)) return false;
  const auto bg = D2D1::ColorF(app.dark ? 0x17191d : 0xffffff);
  const auto fg = D2D1::ColorF(app.dark ? 0xededed : 0x202124);
  ComPtr<ID2D1SolidColorBrush> brush;
  if (FAILED(t->CreateSolidColorBrush(fg, &brush))) return false;
  t->Clear(bg);
  label(t, brush.Get(), L"微软雅黑渲染对照 · 候选为实验方案，非原生 macOS", 16, top, dimensions.width - 32);
  wchar_t summary[256];
  swprintf_s(summary, L"字号 %.0f DIP · DPI %.0f · %s · gamma %.2f · 常规补偿 %.3f px · 粗体 %.3f px · 相位 %d/8 px",
    app.size, app.dpi, app.filter == 1 ? L"direct" : app.filter == 2 ? L"box" : L"Mitchell",
    app.gamma, app.regularWeight, app.boldWeight, app.phase);
  label(t, brush.Get(), summary, 16, top + 32, dimensions.width - 32, 42);
  auto fd = LumaText::Descriptor<lt_frame_desc>(); fd.dpi_x = fd.dpi_y = app.dpi;
  LumaText::Frame frame;
  if (lt_frame_begin(app.renderer.get(), &fd, frame.put()) != LT_OK) return false;
  bool ok = true;
  for (int side = 0; side < 2; ++side) {
    float x = 16 + static_cast<float>(side) * (width + 16);
    float y = top + 84;
    t->PushAxisAlignedClip(D2D1::RectF(x, y, x + width, dimensions.height - 12), D2D1_ANTIALIAS_MODE_ALIASED);
    label(t, brush.Get(), side ? (app.blend ? L"候选 · 已知底色线性合成 + 光学补偿" : L"候选 · 透明路径 + 光学补偿") : L"当前 LumaText · 透明路径 / gamma 0.85", x + 8, y, width - 16, 44);
    y += 52;
    auto start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < app.layouts.size(); ++i) {
      if (i == 0 || i == 4) {
        label(t, brush.Get(), i == 0 ? L"Regular · msyh.ttc" : L"Bold · msyhbd.ttc", x + 8, y, width - 16);
        y += 32;
      }
      auto d = LumaText::Descriptor<lt_draw_text_desc>();
      d.origin_x = x + 8;
      d.origin_y = y + static_cast<float>(app.phase) / 8 * 96 / app.dpi;
      d.foreground = {fg.r, fg.g, fg.b, 1}; d.background = {bg.r, bg.g, bg.b, 1};
      d.background_type = LT_BACKGROUND_TRANSPARENT;
      d.render_config = config(side == 1); d.profile = side ? app.profile.get() : nullptr;
      d.clip_enabled = true;
      d.clip = {x + 8, y - 2, x + width - 8, std::min(y + app.size * 1.65f, dimensions.height - 12)};
      if (d.clip.bottom > d.clip.top && lt_frame_draw_text_layout(frame.get(), app.layouts[i].get(), &d) != LT_OK) ok = false;
      y += app.size * 1.65f + 8;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    wchar_t timing[100]; swprintf_s(timing, L"本帧提交 %.2f ms（含缓存，非 GPU 耗时）", ms);
    label(t, brush.Get(), timing, x + 8, y + 8, width - 16, 42);
    t->PopAxisAlignedClip();
  }
  lt_frame_end(frame.get());
  return ok;
}
HWND control(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
  HWND h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10,
    parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
  SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(app.controlFont), TRUE);
  if (id >= Theme && id <= Blend) app.controls[static_cast<size_t>(id - Theme)] = h;
  return h;
}
void update_control_font(HWND window) {
  HFONT font = CreateFontW(-MulDiv(14, static_cast<int>(GetDpiForWindow(window)), 96),
    0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
  if (!font) return;
  HFONT previous = app.controlFont;
  app.controlFont = font;
  for (HWND h : app.controls) if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  for (HWND h : app.captions) if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  if (previous) DeleteObject(previous);
}
HWND get(int id) { return app.controls[static_cast<size_t>(id - Theme)]; }
void choose(int id, int index) { SendMessageW(get(id), CB_SETCURSEL, index, 0); }
int chosen(int id) { return static_cast<int>(SendMessageW(get(id), CB_GETCURSEL, 0, 0)); }
void defaults() {
  app.updating = true;
  app.dark = false; app.size = 16; app.dpi = 96; app.gamma = .9f;
  app.regularWeight = .02f; app.boldWeight = 0; app.phase = 0; app.filter = 3;
  app.blend = true; app.scroll = 0; SendMessageW(get(Blend), BM_SETCHECK, BST_CHECKED, 0);
  choose(Theme, 0); choose(Size, 2); choose(Dpi, 0); choose(Filter, 2); choose(Phase, 0);
  SetWindowTextW(get(Gamma), L"0.90"); SetWindowTextW(get(Regular), L"0.02"); SetWindowTextW(get(Bold), L"0.00");
  SetWindowTextW(get(Text), sample); app.text = sample; app.dirty = true; app.updating = false;
}
void arrange(HWND window) {
  RECT r; GetClientRect(window, &r);
  float scale = static_cast<float>(GetDpiForWindow(window)) / 96;
  auto move = [scale](HWND h, int x, int y, int w, int height) {
    MoveWindow(h, static_cast<int>(x * scale), static_cast<int>(y * scale), static_cast<int>(w * scale), static_cast<int>(height * scale), TRUE);
  };
  const int positions[] = {16, 140, 264, 388};
  for (int i = 0; i < 8; ++i) {
    int row = i / 4 * 52;
    move(app.captions[static_cast<size_t>(i)], positions[i % 4], 8 + row, 115, 20);
    move(get(Theme + i), positions[i % 4], 30 + row, 112, i < 4 || i == 7 ? 250 : 25);
  }
  move(get(Reset), 525, 29, 105, 28);
  move(get(Blend), 525, 80, 210, 28);
  move(get(Text), 16, 120, std::max(80, static_cast<int>(r.right / scale) - 32), 30);
}
void controls(HWND window) {
  update_control_font(window);
  const wchar_t* captions[] = {L"主题", L"字号 DIP", L"模拟 DPI", L"共同滤波器", L"候选 gamma", L"常规补偿 px", L"粗体补偿 px", L"基线相位"};
  const wchar_t* entries[] = {L"浅色|深色", L"12|14|16|20|24", L"96|120|144|192", L"direct|box|Mitchell", L"", L"", L"", L"0|1|2|3|4|5|6|7"};
  for (int i = 0; i < 8; ++i) {
    app.captions.push_back(control(window, L"STATIC", captions[i], 0, 0));
    bool combo = i < 4 || i == 7;
    HWND h = control(window, combo ? L"COMBOBOX" : L"EDIT", L"", combo ? CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP : WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, Theme + i);
    std::wstring options = entries[i]; size_t pos = 0;
    while (pos < options.size()) {
      size_t end = options.find(L'|', pos);
      std::wstring item = options.substr(pos, end - pos);
      SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
      if (end == std::wstring::npos) break; pos = end + 1;
    }
  }
  control(window, L"BUTTON", L"恢复默认", BS_PUSHBUTTON | WS_TABSTOP, Reset);
  control(window, L"BUTTON", L"统一背景合成", BS_AUTOCHECKBOX | WS_TABSTOP, Blend);
  control(window, L"EDIT", sample, WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, Text);
  defaults(); arrange(window);
}
float number(int id, float fallback, float lo, float hi) {
  wchar_t s[80]; GetWindowTextW(get(id), s, 80); wchar_t* end = nullptr;
  float value = wcstof(s, &end);
  return end != s && *end == 0 && std::isfinite(value) && value >= lo && value <= hi ? value : fallback;
}
void changed(HWND window, int id) {
  if (app.updating) return;
  if (id == Reset) defaults();
  else {
    app.dark = chosen(Theme) == 1;
    app.blend = SendMessageW(get(Blend), BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (id == Theme) {
      app.updating = true; SetWindowTextW(get(Gamma), app.dark ? L"1.00" : L"0.90"); app.updating = false;
    }
    constexpr float sizes[] = {12,14,16,20,24}, dpis[] = {96,120,144,192};
    app.size = sizes[std::clamp(chosen(Size), 0, 4)]; app.dpi = dpis[std::clamp(chosen(Dpi), 0, 3)];
    app.filter = chosen(Filter) + 1; app.phase = std::max(0, chosen(Phase));
    app.gamma = number(Gamma, app.gamma, .1f, 3); app.regularWeight = number(Regular, app.regularWeight, 0, .5f);
    app.boldWeight = number(Bold, app.boldWeight, 0, .5f);
    int length = GetWindowTextLengthW(get(Text)); std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(get(Text), text.data(), length + 1); text.resize(static_cast<size_t>(length)); app.text = std::move(text);
    app.dirty = true;
  }
  InvalidateRect(window, nullptr, FALSE);
}
LRESULT CALLBACK procedure(HWND w, UINT m, WPARAM wp, LPARAM lp) {
  switch (m) {
    case WM_CREATE: controls(w); return 0;
    case WM_COMMAND:
      if (HIWORD(wp) == CBN_SELCHANGE || HIWORD(wp) == EN_CHANGE || LOWORD(wp) == Reset || LOWORD(wp) == Blend) changed(w, LOWORD(wp));
      return 0;
    case WM_SIZE:
      if (app.windowTarget) app.windowTarget->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
      if (!app.captions.empty()) arrange(w); InvalidateRect(w, nullptr, FALSE); return 0;
    case WM_DPICHANGED: {
      auto r = reinterpret_cast<RECT*>(lp); SetWindowPos(w, nullptr, r->left, r->top, r->right-r->left, r->bottom-r->top, SWP_NOZORDER);
      update_control_font(w); arrange(w); return 0;
    }
    case WM_GETMINMAXINFO: {
      auto info = reinterpret_cast<MINMAXINFO*>(lp); float s = static_cast<float>(GetDpiForWindow(w)) / 96;
      info->ptMinTrackSize = {static_cast<LONG>(780*s), static_cast<LONG>(550*s)}; return 0;
    }
    case WM_MOUSEWHEEL:
      app.scroll = std::max(0, app.scroll - static_cast<short>(HIWORD(wp)) / WHEEL_DELTA * 40);
      InvalidateRect(w, nullptr, FALSE); return 0;
    case WM_VSCROLL: {
      SCROLLINFO si{sizeof(si), SIF_ALL}; GetScrollInfo(w, SB_VERT, &si);
      int delta = LOWORD(wp) == SB_LINEUP ? -30 : LOWORD(wp) == SB_LINEDOWN ? 30 : LOWORD(wp) == SB_PAGEUP ? -150 : LOWORD(wp) == SB_PAGEDOWN ? 150 : 0;
      app.scroll = LOWORD(wp) == SB_THUMBTRACK ? si.nTrackPos : std::max(0, app.scroll + delta);
      InvalidateRect(w, nullptr, FALSE); return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps; BeginPaint(w, &ps);
      if (!app.windowTarget) {
        RECT r; GetClientRect(w, &r);
        if (SUCCEEDED(app.factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(w, D2D1::SizeU(r.right, r.bottom)), &app.windowTarget))) attach(app.windowTarget.Get());
      }
      if (app.windowTarget && app.renderer) {
        app.windowTarget->SetDpi(app.dpi, app.dpi); app.windowTarget->BeginDraw();
        app.windowTarget->Clear(D2D1::ColorF(app.dark ? 0x17191d : 0xffffff));
        float top = 164.0f * static_cast<float>(GetDpiForWindow(w)) / app.dpi;
        auto dims = app.windowTarget->GetSize();
        int extent = static_cast<int>(top + 84 + 52 + 64 + 8 * (app.size * 1.65f + 8) + 70);
        int maximum = std::max(0, extent - static_cast<int>(dims.height));
        app.scroll = std::clamp(app.scroll, 0, maximum);
        SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS, 0, extent, static_cast<UINT>(dims.height), app.scroll, 0};
        SetScrollInfo(w, SB_VERT, &si, TRUE);
        app.windowTarget->PushAxisAlignedClip(D2D1::RectF(0, top, dims.width, dims.height), D2D1_ANTIALIAS_MODE_ALIASED);
        panel(app.windowTarget.Get(), top - static_cast<float>(app.scroll));
        app.windowTarget->PopAxisAlignedClip();
        if (app.windowTarget->EndDraw() == D2DERR_RECREATE_TARGET) { app.renderer.reset(); app.windowTarget.Reset(); }
      }
      EndPaint(w, &ps); return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
  }
  return DefWindowProcW(w, m, wp, lp);
}
bool snapshot(const wchar_t* path) {
  ComPtr<IWICImagingFactory> wic; ComPtr<IWICBitmap> bitmap; ComPtr<ID2D1RenderTarget> target;
  const UINT pixelWidth = static_cast<UINT>(1440 * app.dpi / 96);
  const UINT pixelHeight = static_cast<UINT>(900 * app.dpi / 96);
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
      FAILED(wic->CreateBitmap(pixelWidth, pixelHeight, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap)) ||
      FAILED(app.factory->CreateWicBitmapRenderTarget(bitmap.Get(), D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), app.dpi, app.dpi), &target)) || !attach(target.Get())) return false;
  target->BeginDraw(); bool ok = panel(target.Get(), 16); HRESULT hr = target->EndDraw();
  if (!ok || FAILED(hr)) return false;
  ComPtr<IWICStream> stream; ComPtr<IWICBitmapEncoder> encoder; ComPtr<IWICBitmapFrameEncode> frame;
  WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
  return SUCCEEDED(wic->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path, GENERIC_WRITE)) &&
    SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) && SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
    SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(pixelWidth, pixelHeight)) &&
    SUCCEEDED(frame->SetResolution(app.dpi, app.dpi)) && SUCCEEDED(frame->SetPixelFormat(&format)) && SUCCEEDED(frame->WriteSource(bitmap.Get(), nullptr)) &&
    SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}
bool initialize() {
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, app.factory.GetAddressOf())) ||
      FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(app.write.GetAddressOf())))) return false;
  auto c = LumaText::Descriptor<lt_context_desc>(); c.dwrite_factory = app.write.Get(); c.cpu_cache_limit_bytes = 32ull*1024*1024;
  if (lt_context_create(&c, app.context.put()) != LT_OK) return false;
  wchar_t windows[MAX_PATH]; if (!GetWindowsDirectoryW(windows, MAX_PATH)) return false;
  auto face = [&](const wchar_t* name, LumaText::FontFace& out) {
    std::wstring path = std::wstring(windows) + L"\\Fonts\\" + name;
    auto f = LumaText::Descriptor<lt_font_source_desc>(); f.source_type = LT_FONT_SOURCE_FILE; f.file_path = path.c_str();
    return lt_font_face_create(app.context.get(), &f, out.put()) == LT_OK;
  };
  if (!face(L"msyh.ttc", app.regular) || !face(L"msyhbd.ttc", app.bold)) return false;
  lt_font_cascade_entry entries[] = {{app.regular.get(),400,0},{app.bold.get(),700,0}};
  auto cd = LumaText::Descriptor<lt_font_cascade_desc>(); cd.entries = entries; cd.entry_count = 2; cd.allow_system_fallback = false;
  return lt_font_cascade_create(app.context.get(), &cd, app.cascade.put()) == LT_OK &&
    SUCCEEDED(app.write->CreateTextFormat(L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL, 13, L"zh-CN", &app.label));
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
  int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc); std::wstring output;
  for (int i = 1; argv && i < argc; ++i) {
    if (wcscmp(argv[i], L"--snapshot") == 0 && i + 1 < argc) output = argv[++i];
    else if (wcscmp(argv[i], L"--dark") == 0) { app.dark = true; app.gamma = 1; }
    else if (wcscmp(argv[i], L"--no-blend") == 0) app.blend = false;
    else if (wcscmp(argv[i], L"--phase") == 0 && i + 1 < argc) app.phase = std::clamp(_wtoi(argv[++i]), 0, 7);
    else if (wcscmp(argv[i], L"--dpi") == 0 && i + 1 < argc) app.dpi = static_cast<float>(std::clamp(_wtoi(argv[++i]), 96, 192));
    else if (wcscmp(argv[i], L"--size") == 0 && i + 1 < argc) app.size = static_cast<float>(std::clamp(_wtoi(argv[++i]), 12, 24));
  }
  if (argv) LocalFree(argv);
  if (!initialize()) { MessageBoxW(nullptr, L"初始化失败。需要 Windows 微软雅黑 msyh.ttc 与 msyhbd.ttc。", L"LumaText 对照", MB_ICONERROR); return 2; }
  if (!output.empty()) return snapshot(output.c_str()) ? 0 : 3;
  WNDCLASSW wc{}; wc.lpfnWndProc = procedure; wc.hInstance = instance; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = L"LumaTextCompare";
  RegisterClassW(&wc);
  const UINT windowDpi = GetDpiForSystem();
  RECT workArea{};
  if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0)) {
    workArea.right = GetSystemMetrics(SM_CXSCREEN); workArea.bottom = GetSystemMetrics(SM_CYSCREEN);
  }
  const int windowWidth = std::min(MulDiv(1200, static_cast<int>(windowDpi), 96), static_cast<int>(workArea.right - workArea.left));
  const int windowHeight = std::min(MulDiv(850, static_cast<int>(windowDpi), 96), static_cast<int>(workArea.bottom - workArea.top));
  HWND window = CreateWindowExW(0, wc.lpszClassName, L"LumaText · 微软雅黑候选对照", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VSCROLL,
    workArea.left + (workArea.right - workArea.left - windowWidth) / 2,
    workArea.top + (workArea.bottom - workArea.top - windowHeight) / 2,
    windowWidth, windowHeight, nullptr, nullptr, instance, nullptr);
  if (!window) return 4;
  ShowWindow(window, show); MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) { if (!IsDialogMessageW(window, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
  if (app.controlFont) { DeleteObject(app.controlFont); app.controlFont = nullptr; }
  return static_cast<int>(msg.wParam);
}
