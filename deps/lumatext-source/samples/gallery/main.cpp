#include <lumatext/lumatext.hpp>

#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <windowsx.h>

#include <array>
#include <cstdio>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kPingFangFontPath[] =
    L"C:\\Users\\SS\\Desktop\\lumatext\\out\\fonts\\PingFang-Regular.ttf";
constexpr wchar_t kPingFangFamilyName[] = L"PingFang SC Regular";

struct App {
  ComPtr<ID2D1Factory> d2d;
  ComPtr<IDWriteFactory> dwrite;
  ComPtr<ID2D1HwndRenderTarget> target;
  ComPtr<ID2D1SolidColorBrush> brush;
  LumaText::Context context;
  LumaText::Renderer renderer;
  LumaText::FontFace regular;
  LumaText::FontFace bold;
  LumaText::FontFace variable;
  LumaText::FontFace fallback_regular;
  LumaText::FontFace fallback_bold;
  LumaText::FontFace emoji;
  LumaText::FontCascade cascade;
  LumaText::FontFace segoe;
  LumaText::FontFace segoe_bold;
  LumaText::FontFace arial;
  LumaText::FontFace arial_bold;
  LumaText::FontFace consola;
  LumaText::FontFace consola_bold;
  LumaText::FontCascade segoe_cascade;
  LumaText::FontCascade arial_cascade;
  LumaText::FontCascade consola_cascade;
  LumaText::FontFace pingfang;
  LumaText::FontCascade pingfang_cascade;
  bool pingfang_registered = false;
  LumaText::RenderProfile profile;
  bool dark = false;
  bool split = true;
  uint8_t filter = LT_RASTER_FILTER_MITCHELL;
  uint32_t hb_runs = 0;
  uint32_t ft_glyphs = 0;
  uint32_t fallbacks = 0;
  uint32_t cache_hits = 0;
};

App app;

void discard_device_resources() {
  app.renderer.reset();
  app.brush.Reset();
  app.target.Reset();
}

HRESULT create_device_resources(HWND window) {
  if (app.target) return S_OK;
  RECT client{};
  GetClientRect(window, &client);
  const D2D1_SIZE_U size = D2D1::SizeU(
      static_cast<UINT32>(client.right), static_cast<UINT32>(client.bottom));
  HRESULT hr = app.d2d->CreateHwndRenderTarget(
      D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(window, size),
      &app.target);
  if (FAILED(hr)) return hr;
  hr = app.target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &app.brush);
  if (FAILED(hr)) return hr;
  auto desc = LumaText::Descriptor<lt_d2d_desc>();
  desc.render_target = app.target.Get();
  desc.manage_begin_end_draw = false;
  const lt_result result = lt_d2d_renderer_create(app.context.get(), &desc, app.renderer.put());
  return result == LT_OK ? S_OK : E_FAIL;
}

ComPtr<IDWriteTextLayout> make_layout(
    const wchar_t* text, float size, float width,
    const wchar_t* family = L"Microsoft YaHei UI",
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL) {
  ComPtr<IDWriteTextFormat> format;
  app.dwrite->CreateTextFormat(family, nullptr, weight,
                               DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                               size, L"zh-CN", &format);
  ComPtr<IDWriteTextLayout> layout;
  app.dwrite->CreateTextLayout(text, static_cast<UINT32>(wcslen(text)), format.Get(),
                               width, size * 2.3f, &layout);
  return layout;
}

lt_result make_face(const wchar_t* path, LumaText::FontFace& output,
                    const lt_font_axis* axes = nullptr, uint32_t axis_count = 0) {
  auto desc = LumaText::Descriptor<lt_font_source_desc>();
  desc.source_type = LT_FONT_SOURCE_FILE;
  desc.file_path = path;
  desc.axes = axes;
  desc.axis_count = axis_count;
  return lt_font_face_create(app.context.get(), &desc, output.put());
}

bool create_typography_resources() {
  const lt_font_axis weight_axis{
      (static_cast<uint32_t>('w') << 24) | (static_cast<uint32_t>('g') << 16) |
      (static_cast<uint32_t>('h') << 8) | static_cast<uint32_t>('t'), 600.0f};
  if (make_face(L"C:\\Windows\\Fonts\\msyh.ttc", app.regular) != LT_OK ||
      make_face(L"C:\\Windows\\Fonts\\msyhbd.ttc", app.bold) != LT_OK ||
      make_face(L"C:\\Windows\\Fonts\\arial.ttf", app.fallback_regular) != LT_OK ||
      make_face(L"C:\\Windows\\Fonts\\arialbd.ttf", app.fallback_bold) != LT_OK ||
      make_face(L"C:\\Windows\\Fonts\\seguiemj.ttf", app.emoji) != LT_OK) {
    return false;
  }
  const bool has_variable = make_face(L"C:\\Windows\\Fonts\\NotoSansSC-VF.ttf",
                                      app.variable, &weight_axis, 1) == LT_OK;
  std::array<lt_font_cascade_entry, 6> entries{{
      {app.regular.get(), 400, 0},
      {has_variable ? app.variable.get() : app.regular.get(), 600, 0},
      {app.bold.get(), 700, 0},
      {app.fallback_regular.get(), 400, 0},
      {app.fallback_bold.get(), 700, 0},
      {app.emoji.get(), 400, 0},
  }};
  auto create_cascade = [&](const auto& cascade_entries,
                            LumaText::FontCascade& output) {
    auto cascade_desc = LumaText::Descriptor<lt_font_cascade_desc>();
    cascade_desc.entries = cascade_entries.data();
    cascade_desc.entry_count = static_cast<uint32_t>(cascade_entries.size());
    cascade_desc.allow_system_fallback = false;
    return lt_font_cascade_create(app.context.get(), &cascade_desc, output.put()) == LT_OK;
  };
  if (!create_cascade(entries, app.cascade)) {
    return false;
  }

  const auto make_optional_font = [&](const wchar_t* path, LumaText::FontFace& face) {
    return make_face(path, face) == LT_OK;
  };
  const bool has_segoe = make_optional_font(L"C:\\Windows\\Fonts\\segoeui.ttf", app.segoe) &&
      make_optional_font(L"C:\\Windows\\Fonts\\segoeuib.ttf", app.segoe_bold);
  const bool has_arial = make_optional_font(L"C:\\Windows\\Fonts\\arial.ttf", app.arial) &&
      make_optional_font(L"C:\\Windows\\Fonts\\arialbd.ttf", app.arial_bold);
  const bool has_consola = make_optional_font(L"C:\\Windows\\Fonts\\consola.ttf", app.consola) &&
      make_optional_font(L"C:\\Windows\\Fonts\\consolab.ttf", app.consola_bold);
  if (has_segoe) {
    std::array<lt_font_cascade_entry, 5> segoe_entries{{
        {app.segoe.get(), 400, 0}, {app.segoe_bold.get(), 700, 0},
        {app.regular.get(), 400, 0}, {app.bold.get(), 700, 0}, {app.emoji.get(), 400, 0}}};
    if (!create_cascade(segoe_entries, app.segoe_cascade)) return false;
  }
  if (has_arial) {
    std::array<lt_font_cascade_entry, 5> arial_entries{{
        {app.arial.get(), 400, 0}, {app.arial_bold.get(), 700, 0},
        {app.regular.get(), 400, 0}, {app.bold.get(), 700, 0}, {app.emoji.get(), 400, 0}}};
    if (!create_cascade(arial_entries, app.arial_cascade)) return false;
  }
  if (has_consola) {
    std::array<lt_font_cascade_entry, 5> consola_entries{{
        {app.consola.get(), 400, 0}, {app.consola_bold.get(), 700, 0},
        {app.regular.get(), 400, 0}, {app.bold.get(), 700, 0}, {app.emoji.get(), 400, 0}}};
    if (!create_cascade(consola_entries, app.consola_cascade)) return false;
  }

  if (make_optional_font(kPingFangFontPath, app.pingfang)) {
    std::array<lt_font_cascade_entry, 3> pingfang_entries{{
        {app.pingfang.get(), 400, 0},
        {app.regular.get(), 400, 0},
        {app.emoji.get(), 400, 0},
    }};
    if (!create_cascade(pingfang_entries, app.pingfang_cascade)) return false;
  }

  auto profile_desc = LumaText::Descriptor<lt_render_profile_desc>();
  profile_desc.light = LumaText::Descriptor<lt_render_config>();
  profile_desc.light.coverage_gamma = 0.85f;
  profile_desc.light.coverage_contrast = 1.00f;
  profile_desc.light.stem_strength = 0.00f;
  profile_desc.dark = LumaText::Descriptor<lt_render_config>();
  profile_desc.dark.coverage_gamma = 0.85f;
  profile_desc.dark.coverage_contrast = 1.00f;
  profile_desc.dark.stem_strength = 0.00f;
  profile_desc.regular_optical_weight = 0.06f;
  profile_desc.bold_optical_weight = 0.0f;
  return lt_render_profile_create(&profile_desc, app.profile.put()) == LT_OK;
}

LumaText::TextLayout make_luma_layout(const wchar_t* text, float size,
                                      float width, uint16_t weight,
                                      lt_font_cascade* cascade = nullptr) {
  auto style = LumaText::Descriptor<lt_text_style>();
  style.cascade = cascade ? cascade : app.cascade.get();
  style.font_size = size;
  style.weight = weight;
  auto desc = LumaText::Descriptor<lt_text_layout_desc>();
  desc.text = text;
  desc.text_length = static_cast<uint32_t>(wcslen(text));
  desc.base_style = style;
  desc.locale = "zh-CN";
  desc.direction = LT_TEXT_DIRECTION_AUTO;
  desc.alignment = LT_TEXT_ALIGNMENT_START;
  desc.ellipsis = LT_TEXT_ELLIPSIS_END;
  desc.max_width = width;
  LumaText::TextLayout result;
  lt_text_layout_create(app.context.get(), &desc, result.put());
  return result;
}

void draw_native(IDWriteTextLayout* layout, float x, float y, D2D1_COLOR_F color) {
  app.brush->SetColor(color);
  app.target->DrawTextLayout(D2D1::Point2F(x, y), layout, app.brush.Get(),
                             D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void draw_lumatext(const lt_text_layout* layout, float x, float y,
                   D2D1_COLOR_F color, D2D1_COLOR_F background,
                   float dpi_x, float dpi_y,
                   uint8_t filter = LT_RASTER_FILTER_MITCHELL,
                   float clip_width = 0.0f,
                   float clip_height = 0.0f) {
  if (!layout) return;
  auto frame_desc = LumaText::Descriptor<lt_frame_desc>();
  frame_desc.dpi_x = dpi_x;
  frame_desc.dpi_y = dpi_y;
  LumaText::Frame frame;
  if (lt_frame_begin(app.renderer.get(), &frame_desc, frame.put()) != LT_OK) return;

  auto draw = LumaText::Descriptor<lt_draw_text_desc>();
  draw.origin_x = x;
  draw.origin_y = y;
  draw.foreground = {color.r, color.g, color.b, color.a};
  draw.background = {background.r, background.g, background.b, background.a};
  draw.background_type = LT_BACKGROUND_SOLID;
  draw.render_config = LumaText::Descriptor<lt_render_config>();
  draw.render_config.coverage_gamma = 0.85f;
  draw.render_config.coverage_contrast = 1.00f;
  draw.render_config.stem_strength = 0.00f;
  draw.render_config.raster_filter = filter;
  draw.profile = nullptr;
  if (clip_width > 0.0f && clip_height > 0.0f) {
    draw.clip_enabled = true;
    draw.clip = {x, y - 4.0f, x + clip_width, y + clip_height};
  }
  lt_frame_draw_text_layout(frame.get(), layout, &draw);
  auto stats = LumaText::Descriptor<lt_frame_stats>();
  if (lt_frame_get_stats(frame.get(), &stats) == LT_OK) {
    app.hb_runs += stats.harfbuzz_runs;
    app.ft_glyphs += stats.freetype_glyphs;
    app.fallbacks += stats.compatibility_fallback_runs;
    app.cache_hits += stats.glyph_cache_hits;
  }
  lt_frame_end(frame.get());
}

void paint(HWND window) {
  PAINTSTRUCT paint{};
  BeginPaint(window, &paint);
  if (FAILED(create_device_resources(window))) {
    EndPaint(window, &paint);
    return;
  }

  float dpi_x = 96.0f;
  float dpi_y = 96.0f;
  app.target->GetDpi(&dpi_x, &dpi_y);
  const D2D1_COLOR_F background = app.dark
      ? D2D1::ColorF(0x17191c) : D2D1::ColorF(0xf6f7f8);
  const D2D1_COLOR_F foreground = app.dark
      ? D2D1::ColorF(0xe8e9eb) : D2D1::ColorF(0x17191c);
  const D2D1_COLOR_F muted = app.dark
      ? D2D1::ColorF(0x8f969e) : D2D1::ColorF(0x606770);

  app.hb_runs = app.ft_glyphs = app.fallbacks = app.cache_hits = 0;
  app.target->BeginDraw();
  app.target->Clear(background);
  auto header = make_layout(app.split ? L"DirectWrite compatibility                 CoreText-like"
                                      : L"LumaText CoreText-like",
                            13.0f, 1100.0f);
  draw_native(header.Get(), 28.0f, 18.0f, muted);

  struct GalleryRow {
    const wchar_t* label;
    float size;
    const wchar_t* text;
    const wchar_t* family;
    DWRITE_FONT_WEIGHT weight;
    lt_font_cascade* cascade;
  };
  std::vector<GalleryRow> rows;
  rows.push_back({L"Microsoft YaHei UI · CJK regular", 13.0f,
      L"微软雅黑 UI：中文笔画、标点“引号”，ABC xyz 125%",
      L"Microsoft YaHei UI", DWRITE_FONT_WEIGHT_NORMAL, app.cascade.get()});
  if (app.segoe_cascade) rows.push_back({L"Segoe UI · Latin + CJK fallback", 14.0f,
      L"Segoe UI: CoreText-like edges / 文件名 0123456789",
      L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, app.segoe_cascade.get()});
  if (app.arial_cascade) rows.push_back({L"Arial · Latin + CJK fallback", 14.0f,
      L"Arial: punctuation, @mail.com, $1,234.50, 中文 fallback",
      L"Arial", DWRITE_FONT_WEIGHT_NORMAL, app.arial_cascade.get()});
  if (app.consola_cascade) rows.push_back({L"Consolas · tabular digits", 14.0f,
      L"Consolas  0123456789  0xA8  125%  CJK fallback",
      L"Consolas", DWRITE_FONT_WEIGHT_NORMAL, app.consola_cascade.get()});
  if (app.pingfang_cascade) rows.push_back({L"PingFang SC · downloaded font", 14.0f,
      L"苹方：项目计划 中文标点，引号 Aa 0123456789",
      kPingFangFamilyName, DWRITE_FONT_WEIGHT_NORMAL, app.pingfang_cascade.get()});
  rows.push_back({L"Microsoft YaHei UI · bold", 16.0f,
      L"粗体观感测试 — Quick Look / README.md / ¥1,234.50",
      L"Microsoft YaHei UI", DWRITE_FONT_WEIGHT_BOLD, app.cascade.get()});
  rows.push_back({L"Mixed fallback · emoji + scripts", 14.0f,
      L"Fallback: العربية Ελληνικά עברית emoji 😀 文件名",
      L"Microsoft YaHei UI", DWRITE_FONT_WEIGHT_NORMAL, app.cascade.get()});

  float y = 60.0f;
  for (const auto& row : rows) {
    auto label = make_layout(row.label, 11.0f, 510.0f, L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD);
    draw_native(label.Get(), 28.0f, y, muted);
    auto layout = make_layout(row.text, row.size, 510.0f, row.family, row.weight);
    auto luma_layout = make_luma_layout(
        row.text, row.size, 510.0f, static_cast<uint16_t>(row.weight), row.cascade);
    if (app.split) draw_native(layout.Get(), 28.0f, y + 18.0f, foreground);
    draw_lumatext(luma_layout.get(), app.split ? 570.0f : 28.0f, y + 18.0f,
                  foreground, background, dpi_x, dpi_y, app.filter);
    y += 76.0f;
  }

  const float microscope_y = y + 18.0f;
  auto microscope_layout = make_luma_layout(
      L"细笔画 Aa 012345 中文", 20.0f, 340.0f,
      DWRITE_FONT_WEIGHT_NORMAL, app.cascade.get());
  const std::array<uint8_t, 3> microscope_filters{{
      LT_RASTER_FILTER_DIRECT, LT_RASTER_FILTER_BOX, LT_RASTER_FILTER_MITCHELL}};
  const std::array<const wchar_t*, 3> microscope_names{{
      L"DIRECT grayscale", L"4X BOX / AREA", L"4X MITCHELL"}};
  for (size_t index = 0; index < microscope_filters.size(); ++index) {
    const float x = 28.0f + static_cast<float>(index) * 372.0f;
    auto label = make_layout(microscope_names[index], 11.0f, 340.0f,
                             L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD);
    draw_native(label.Get(), x, microscope_y - 18.0f, muted);
    draw_lumatext(microscope_layout.get(), x, microscope_y,
                  foreground, background, dpi_x, dpi_y, microscope_filters[index],
                  340.0f, 42.0f);
  }
  y = microscope_y + 46.0f;

  wchar_t stats_text[256]{};
  swprintf_s(stats_text, L"HB runs %u    FT glyphs %u    fallback %u    cache hits %u",
             app.hb_runs, app.ft_glyphs, app.fallbacks, app.cache_hits);
  wchar_t filter_text[128]{};
  const wchar_t* filter_name = app.filter == LT_RASTER_FILTER_DIRECT ? L"direct" :
      app.filter == LT_RASTER_FILTER_BOX ? L"4x box" : L"4x Mitchell";
  swprintf_s(filter_text, L"Filter %s   (1 direct / 2 box / 3 Mitchell)", filter_name);
  auto filter_layout = make_layout(filter_text, 12.0f, 900.0f, L"Segoe UI");
  draw_native(filter_layout.Get(), 28.0f, y + 8.0f, muted);
  auto stats = make_layout(stats_text, 12.0f, 900.0f, L"Segoe UI");
  draw_native(stats.Get(), 570.0f, y + 8.0f, muted);
  HRESULT hr = app.target->EndDraw();
  if (hr == D2DERR_RECREATE_TARGET) discard_device_resources();
  EndPaint(window, &paint);
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_PAINT:
      paint(window);
      return 0;
    case WM_SIZE:
      if (app.target) app.target->Resize(D2D1::SizeU(LOWORD(lparam), HIWORD(lparam)));
      InvalidateRect(window, nullptr, FALSE);
      return 0;
    case WM_DPICHANGED: {
      const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
      SetWindowPos(window, nullptr, suggested->left, suggested->top,
                   suggested->right - suggested->left, suggested->bottom - suggested->top,
                   SWP_NOACTIVATE | SWP_NOZORDER);
      discard_device_resources();
      return 0;
    }
    case WM_KEYDOWN:
      if (wparam == VK_SPACE) app.split = !app.split;
      if (wparam == 'D') app.dark = !app.dark;
      if (wparam == '1') app.filter = LT_RASTER_FILTER_DIRECT;
      if (wparam == '2') app.filter = LT_RASTER_FILTER_BOX;
      if (wparam == '3') app.filter = LT_RASTER_FILTER_MITCHELL;
      InvalidateRect(window, nullptr, FALSE);
      return 0;
    case WM_DESTROY:
      discard_device_resources();
      if (app.pingfang_registered) {
        RemoveFontResourceExW(kPingFangFontPath, FR_PRIVATE, nullptr);
        app.pingfang_registered = false;
      }
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProc(window, message, wparam, lparam);
  }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  app.pingfang_registered = AddFontResourceExW(kPingFangFontPath, FR_PRIVATE, nullptr) != 0;
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, app.d2d.GetAddressOf())) ||
      FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(app.dwrite.GetAddressOf())))) {
    return 1;
  }
  auto context_desc = LumaText::Descriptor<lt_context_desc>();
  context_desc.dwrite_factory = app.dwrite.Get();
  context_desc.cpu_cache_limit_bytes = 32ull * 1024ull * 1024ull;
  if (lt_context_create(&context_desc, app.context.put()) != LT_OK) return 2;
  if (!create_typography_resources()) return 3;

  const wchar_t* class_name = L"LumaTextGalleryWindow";
  WNDCLASSW window_class{};
  window_class.lpfnWndProc = window_proc;
  window_class.hInstance = instance;
  window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
  window_class.lpszClassName = class_name;
  RegisterClassW(&window_class);
  HWND window = CreateWindowExW(0, class_name, L"LumaText 0.1 A/B Gallery",
                                 WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                 1160, 760, nullptr, nullptr, instance, nullptr);
  if (!window) return 4;
  constexpr int client_width_dip = 1160;
  constexpr int client_height_dip = 700;
  const UINT window_dpi = GetDpiForWindow(window);
  RECT client_rect{};
  RECT window_rect{};
  GetClientRect(window, &client_rect);
  GetWindowRect(window, &window_rect);
  const int non_client_width =
      (window_rect.right - window_rect.left) - (client_rect.right - client_rect.left);
  const int non_client_height =
      (window_rect.bottom - window_rect.top) - (client_rect.bottom - client_rect.top);
  SetWindowPos(window, nullptr, 0, 0,
               MulDiv(client_width_dip, window_dpi, 96) + non_client_width,
               MulDiv(client_height_dip, window_dpi, 96) + non_client_height,
               SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
  ShowWindow(window, show);
  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return static_cast<int>(message.wParam);
}
