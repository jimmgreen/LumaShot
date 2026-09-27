#include "internal.hpp"
#include <lumatext/lumatext.hpp>

#include <d2d1helper.h>
#include <wincodec.h>

#include <bcrypt.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {

struct Case {
  const char* name;
  const wchar_t* text;
  float size;
  uint16_t weight;
  bool fallback;
};

constexpr std::array<Case, 5> corpus{{
    {"quick_12_regular", L"Quick Look", 12.0f, 400, false},
    {"quick_16_bold", L"Quick Look", 16.0f, 700, false},
    {"latin_13_regular", L"Mixed filename README.md 0123456789", 13.0f, 400, false},
    {"cjk_14_regular", L"项目计划 2026-08-24.txt 中文标点，引号", 14.0f, 400, false},
    {"cjk_16_bold", L"粗体观感 Quick Look 文件列表", 16.0f, 700, false},
}};
constexpr std::array<float, 5> scales{{1.0f, 1.25f, 1.5f, 2.0f, 3.0f}};

std::wstring argument(int argc, wchar_t** argv, const wchar_t* name) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (wcscmp(argv[index], name) == 0) return argv[index + 1];
  }
  return {};
}

float float_argument(int argc, wchar_t** argv, const wchar_t* name, float fallback) {
  const std::wstring value = argument(argc, argv, name);
  if (value.empty()) return fallback;
  wchar_t* end = nullptr;
  const float parsed = std::wcstof(value.c_str(), &end);
  return end && *end == L'\0' && std::isfinite(parsed) ? parsed : fallback;
}

uint8_t filter_argument(int argc, wchar_t** argv) {
  const std::wstring value = argument(argc, argv, L"--filter");
  if (value == L"direct") return LT_RASTER_FILTER_DIRECT;
  if (value == L"box") return LT_RASTER_FILTER_BOX;
  return LT_RASTER_FILTER_MITCHELL;
}

std::string utf8(const wchar_t* text) {
  if (!text) return {};
  const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  std::string result(static_cast<size_t>(std::max(0, length - 1)), '\0');
  if (length > 1) {
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), length - 1, nullptr, nullptr);
  }
  return result;
}

std::string json_string(std::string_view value) {
  std::string result = "\"";
  for (unsigned char character : value) {
    switch (character) {
      case '\\': result += "\\\\"; break;
      case '"': result += "\\\""; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:
        if (character < 0x20) {
          char escaped[8]{};
          sprintf_s(escaped, "\\u%04x", character);
          result += escaped;
        } else {
          result += static_cast<char>(character);
        }
    }
  }
  result += '"';
  return result;
}

std::string sha256(const std::wstring& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return {};
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD object_size = 0;
  DWORD result_size = 0;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                  nullptr, 0) != 0 ||
      BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
          reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
          &result_size, 0) != 0) {
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return {};
  }
  std::vector<uint8_t> object(object_size);
  std::array<uint8_t, 32> digest{};
  if (BCryptCreateHash(algorithm, &hash, object.data(), object_size,
                       nullptr, 0, 0) != 0) {
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return {};
  }
  std::array<char, 64 * 1024> buffer{};
  while (input) {
    input.read(buffer.data(), buffer.size());
    const std::streamsize received = input.gcount();
    if (received > 0) {
      BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()),
                     static_cast<ULONG>(received), 0);
    }
  }
  BCryptFinishHash(hash, digest.data(), digest.size(), 0);
  BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (uint8_t value : digest) output << std::setw(2) << static_cast<unsigned>(value);
  return output.str();
}

bool write_png(IWICImagingFactory* factory, IWICBitmap* bitmap,
               const std::filesystem::path& path, uint32_t width, uint32_t height) {
  lt::ComPtr<IWICStream> stream;
  lt::ComPtr<IWICBitmapEncoder> encoder;
  lt::ComPtr<IWICBitmapFrameEncode> frame;
  if (FAILED(factory->CreateStream(&stream)) ||
      FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
      FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
      FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
      FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
      FAILED(frame->SetSize(width, height))) {
    return false;
  }
  WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
  if (FAILED(frame->SetPixelFormat(&format)) ||
      FAILED(frame->WriteSource(bitmap, nullptr)) || FAILED(frame->Commit()) ||
      FAILED(encoder->Commit())) {
    return false;
  }
  return true;
}

std::string scale_name(float scale) {
  char value[16]{};
  sprintf_s(value, "%.2f", scale);
  std::string result = value;
  std::replace(result.begin(), result.end(), '.', '_');
  return result;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  const std::wstring regular_path = argument(argc, argv, L"--regular");
  const std::wstring bold_path = argument(argc, argv, L"--bold");
  const std::filesystem::path output_path = argument(argc, argv, L"--output");
  const float gamma = float_argument(argc, argv, L"--gamma", 0.43f);
  const float contrast = float_argument(argc, argv, L"--contrast", 1.92f);
  const float stem = float_argument(argc, argv, L"--stem", 0.0f);
  const uint8_t filter = filter_argument(argc, argv);
  if (regular_path.empty() || bold_path.empty() || output_path.empty()) return 2;
  std::filesystem::create_directories(output_path);
  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 3;

  lt::ComPtr<IWICImagingFactory> wic;
  lt::ComPtr<ID2D1Factory> d2d;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&wic))) ||
      FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                               d2d.GetAddressOf()))) {
    return 4;
  }
  auto context_desc = LumaText::Descriptor<lt_context_desc>();
  LumaText::Context context;
  if (lt_context_create(&context_desc, context.put()) != LT_OK) return 5;
  auto source = LumaText::Descriptor<lt_font_source_desc>();
  source.source_type = LT_FONT_SOURCE_FILE;
  source.file_path = regular_path.c_str();
  LumaText::FontFace regular;
  if (lt_font_face_create(context.get(), &source, regular.put()) != LT_OK) return 6;
  source.file_path = bold_path.c_str();
  LumaText::FontFace bold;
  if (lt_font_face_create(context.get(), &source, bold.put()) != LT_OK) return 7;
  const lt_font_cascade_entry entries[]{{regular.get(), 400, 0}, {bold.get(), 700, 0}};
  auto cascade_desc = LumaText::Descriptor<lt_font_cascade_desc>();
  cascade_desc.entries = entries;
  cascade_desc.entry_count = ARRAYSIZE(entries);
  LumaText::FontCascade cascade;
  if (lt_font_cascade_create(context.get(), &cascade_desc, cascade.put()) != LT_OK) return 8;

  std::ofstream manifest(output_path / "manifest.json", std::ios::binary);
  manifest << "{\n  \"renderer\": \"LumaText\",\n"
           << "  \"osBuild\": \"Windows\",\n"
           << "  \"coverageGamma\": " << gamma << ",\n"
           << "  \"coverageContrast\": " << contrast << ",\n"
           << "  \"stemStrength\": " << stem << ",\n"
           << "  \"rasterFilter\": " << static_cast<unsigned>(filter) << ",\n"
           << "  \"regularSHA256\": " << json_string(sha256(regular_path)) << ",\n"
           << "  \"boldSHA256\": " << json_string(sha256(bold_path)) << ",\n"
           << "  \"records\": [\n";
  bool first_record = true;
  for (const auto& item : corpus) {
    for (float scale : scales) {
      for (const char* background_name : {"light", "dark"}) {
        const uint32_t pixel_width = static_cast<uint32_t>(std::ceil(800.0f * scale));
        const uint32_t pixel_height = static_cast<uint32_t>(std::ceil(80.0f * scale));
        lt::ComPtr<IWICBitmap> bitmap;
        lt::ComPtr<ID2D1RenderTarget> target;
        if (FAILED(wic->CreateBitmap(pixel_width, pixel_height,
                GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap)) ||
            FAILED(d2d->CreateWicBitmapRenderTarget(bitmap.Get(),
                D2D1::RenderTargetProperties(), &target))) {
          return 9;
        }
        target->SetDpi(96.0f * scale, 96.0f * scale);
        target->BeginDraw();
        if (strcmp(background_name, "light") == 0) {
          target->Clear(D2D1::ColorF(0.965f, 0.969f, 0.973f, 1.0f));
        } else {
          target->Clear(D2D1::ColorF(0.09f, 0.098f, 0.11f, 1.0f));
        }
        if (FAILED(target->EndDraw())) return 10;
        auto renderer_desc = LumaText::Descriptor<lt_d2d_desc>();
        renderer_desc.render_target = target.Get();
        renderer_desc.manage_begin_end_draw = true;
        LumaText::Renderer renderer;
        if (lt_d2d_renderer_create(context.get(), &renderer_desc, renderer.put()) != LT_OK) return 11;
        auto style = LumaText::Descriptor<lt_text_style>();
        style.cascade = cascade.get();
        style.font_size = item.size;
        style.weight = item.weight;
        auto layout_desc = LumaText::Descriptor<lt_text_layout_desc>();
        layout_desc.text = item.text;
        layout_desc.text_length = static_cast<uint32_t>(wcslen(item.text));
        layout_desc.base_style = style;
        layout_desc.locale = "zh-CN";
        layout_desc.direction = LT_TEXT_DIRECTION_AUTO;
        layout_desc.max_width = 780.0f;
        LumaText::TextLayout layout;
        if (lt_text_layout_create(context.get(), &layout_desc, layout.put()) != LT_OK) return 12;
        auto frame_desc = LumaText::Descriptor<lt_frame_desc>();
        frame_desc.dpi_x = frame_desc.dpi_y = 96.0f * scale;
        LumaText::Frame draw_frame;
        if (lt_frame_begin(renderer.get(), &frame_desc, draw_frame.put()) != LT_OK) return 13;
        auto draw = LumaText::Descriptor<lt_draw_text_desc>();
        draw.origin_x = draw.origin_y = 8.0f;
        if (strcmp(background_name, "light") == 0) {
          draw.foreground = {0.09f, 0.098f, 0.11f, 1.0f};
          draw.background = {0.965f, 0.969f, 0.973f, 1.0f};
        } else {
          draw.foreground = {0.91f, 0.918f, 0.929f, 1.0f};
          draw.background = {0.09f, 0.098f, 0.11f, 1.0f};
        }
        draw.background_type = LT_BACKGROUND_SOLID;
        draw.render_config = LumaText::Descriptor<lt_render_config>();
        draw.render_config.coverage_gamma = gamma;
        draw.render_config.coverage_contrast = contrast;
        draw.render_config.stem_strength = stem;
        draw.render_config.raster_filter = filter;
        if (lt_frame_draw_text_layout(draw_frame.get(), layout.get(), &draw) != LT_OK ||
            lt_frame_end(draw_frame.get()) != LT_OK) return 14;
        const std::string image_name = std::string(item.name) + "__" + background_name +
            "__" + scale_name(scale) + ".png";
        if (!write_png(wic.Get(), bitmap.Get(), output_path / image_name,
                       pixel_width, pixel_height)) return 15;

        lt::ComPtr<IWICBitmap> mask_bitmap;
        lt::ComPtr<ID2D1RenderTarget> mask_target;
        if (FAILED(wic->CreateBitmap(pixel_width, pixel_height,
                GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &mask_bitmap)) ||
            FAILED(d2d->CreateWicBitmapRenderTarget(mask_bitmap.Get(),
                D2D1::RenderTargetProperties(), &mask_target))) {
          return 16;
        }
        mask_target->SetDpi(96.0f * scale, 96.0f * scale);
        mask_target->BeginDraw();
        mask_target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
        if (FAILED(mask_target->EndDraw())) return 17;
        auto mask_renderer_desc = LumaText::Descriptor<lt_d2d_desc>();
        mask_renderer_desc.render_target = mask_target.Get();
        mask_renderer_desc.manage_begin_end_draw = true;
        LumaText::Renderer mask_renderer;
        if (lt_d2d_renderer_create(context.get(), &mask_renderer_desc,
                                    mask_renderer.put()) != LT_OK) return 18;
        LumaText::Frame mask_frame;
        if (lt_frame_begin(mask_renderer.get(), &frame_desc, mask_frame.put()) != LT_OK) return 19;
        auto mask_draw = LumaText::Descriptor<lt_draw_text_desc>();
        mask_draw.origin_x = mask_draw.origin_y = 8.0f;
        mask_draw.foreground = {1.0f, 1.0f, 1.0f, 1.0f};
        mask_draw.background_type = LT_BACKGROUND_TRANSPARENT;
        mask_draw.render_config = draw.render_config;
        if (lt_frame_draw_text_layout(mask_frame.get(), layout.get(), &mask_draw) != LT_OK ||
            lt_frame_end(mask_frame.get()) != LT_OK) return 20;
        const std::string mask_name = std::string(item.name) + "__" + background_name +
            "__" + scale_name(scale) + ".mask.png";
        if (!write_png(wic.Get(), mask_bitmap.Get(), output_path / mask_name,
                       pixel_width, pixel_height)) return 21;

        auto metrics = LumaText::Descriptor<lt_text_metrics>();
        lt_text_layout_get_metrics(layout.get(), &metrics);
        if (!first_record) manifest << ",\n";
        first_record = false;
        manifest << "    {\"name\":" << json_string(item.name)
                 << ",\"text\":" << json_string(utf8(item.text))
                 << ",\"weight\":" << item.weight << ",\"size\":" << item.size
                 << ",\"scale\":" << scale << ",\"background\":"
                 << json_string(background_name) << ",\"image\":" << json_string(image_name)
                 << ",\"mask\":" << json_string(mask_name)
                 << ",\"width\":" << metrics.width << ",\"ascent\":" << metrics.ascent
                 << ",\"descent\":" << metrics.descent << ",\"leading\":"
                 << metrics.leading << ",\"fallbackCase\":false,\"glyphs\":[";
        bool first_glyph = true;
        for (const auto& run : layout.get()->runs) {
          for (const auto& glyph : run.glyphs) {
            if (!first_glyph) manifest << ',';
            first_glyph = false;
            manifest << "{\"glyph\":" << glyph.glyph_index << ",\"cluster\":"
                     << glyph.cluster << ",\"x\":" << glyph.x << ",\"y\":" << glyph.y
                     << ",\"advance\":" << glyph.advance << ",\"font\":\"pinned\"}";
          }
        }
        manifest << "]}";
      }
    }
  }
  manifest << "\n  ]\n}\n";
  return 0;
}
