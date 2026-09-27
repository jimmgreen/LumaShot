#include <lumatext/lumatext.hpp>

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdio>

using Microsoft::WRL::ComPtr;

#define CHECK(expression)                                                        \
  do {                                                                           \
    if (!(expression)) {                                                         \
      std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
      return __LINE__;                                                           \
    }                                                                            \
  } while (false)

int run() {
  ComPtr<IWICImagingFactory> wic;
  CHECK(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&wic))));
  ComPtr<IWICBitmap> bitmap;
  CHECK(SUCCEEDED(wic->CreateBitmap(640, 160, GUID_WICPixelFormat32bppPBGRA,
                                    WICBitmapCacheOnLoad, &bitmap)));
  ComPtr<ID2D1Factory> d2d;
  CHECK(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                    d2d.GetAddressOf())));
  ComPtr<ID2D1RenderTarget> target;
  CHECK(SUCCEEDED(d2d->CreateWicBitmapRenderTarget(
      bitmap.Get(), D2D1::RenderTargetProperties(), &target)));
  ComPtr<IDWriteFactory> dwrite;
  CHECK(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                      reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()))));

  auto context_desc = LumaText::Descriptor<lt_context_desc>();
  context_desc.dwrite_factory = dwrite.Get();
  LumaText::Context context;
  CHECK(lt_context_create(&context_desc, context.put()) == LT_OK);
  auto renderer_desc = LumaText::Descriptor<lt_d2d_desc>();
  renderer_desc.render_target = target.Get();
  renderer_desc.manage_begin_end_draw = true;
  LumaText::Renderer renderer;
  CHECK(lt_d2d_renderer_create(context.get(), &renderer_desc, renderer.put()) == LT_OK);

  ComPtr<IDWriteTextFormat> format;
  CHECK(SUCCEEDED(dwrite->CreateTextFormat(
      L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"zh-CN", &format)));
  constexpr wchar_t text[] = L"LumaText 中文 123";
  ComPtr<IDWriteTextLayout> layout;
  CHECK(SUCCEEDED(dwrite->CreateTextLayout(text, ARRAYSIZE(text) - 1, format.Get(),
                                           600.0f, 100.0f, &layout)));
  auto frame_desc = LumaText::Descriptor<lt_frame_desc>();
  frame_desc.dpi_x = 96.0f;
  frame_desc.dpi_y = 96.0f;
  LumaText::Frame frame;
  CHECK(lt_frame_begin(renderer.get(), &frame_desc, frame.put()) == LT_OK);
  auto draw_desc = LumaText::Descriptor<lt_draw_text_desc>();
  draw_desc.origin_x = 12.0f;
  draw_desc.origin_y = 12.0f;
  draw_desc.foreground = {0.0f, 0.0f, 0.0f, 1.0f};
  CHECK(lt_frame_draw_layout(frame.get(), layout.Get(), &draw_desc) == LT_OK);
  CHECK(lt_frame_end(frame.get()) == LT_OK);

  WICRect area{0, 0, 640, 160};
  ComPtr<IWICBitmapLock> lock;
  CHECK(SUCCEEDED(bitmap->Lock(&area, WICBitmapLockRead, &lock)));
  UINT size = 0;
  BYTE* pixels = nullptr;
  CHECK(SUCCEEDED(lock->GetDataPointer(&size, &pixels)));
  bool nonzero = false;
  for (UINT i = 0; i < size; ++i) nonzero = nonzero || pixels[i] != 0;
  CHECK(nonzero);
  return 0;
}

int main() {
  CHECK(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
  const int result = run();
  CoUninitialize();
  return result;
}
