#pragma once
#include <d2d1.h>
#include <wrl/client.h>
#include <cstdint>

namespace lumashot {
// Direct2D software rendering straight into caller-owned 32bpp premultiplied
// BGRA memory (normally a DibSurface). ID2D1DCRenderTarget round-trips every
// frame through GDI: about 4 ns per pixel, 26 ms for an empty 4K frame before
// anything is drawn. A WIC bitmap render target over the same memory draws in
// place and has no per-frame copy. The memory must outlive the target and all
// resources created from it. Text and geometry rasterize exactly as on a DC
// target, since both are the same software rasterizer.
Microsoft::WRL::ComPtr<ID2D1RenderTarget> CreateMemoryRenderTarget(
    ID2D1Factory* factory, uint32_t* pixels, int width, int height, int stride_pixels,
    D2D1_ALPHA_MODE alpha = D2D1_ALPHA_MODE_PREMULTIPLIED);
// A memory target is fixed to its buffer, so a resize means a new target.
// Software targets of one factory share bitmaps: this re-homes `bitmap` onto
// `target` without uploading pixels again. On failure the bitmap is released
// (callers re-create it from their source) and false is returned.
bool ShareBitmap(ID2D1RenderTarget* target, Microsoft::WRL::ComPtr<ID2D1Bitmap>& bitmap);
}
