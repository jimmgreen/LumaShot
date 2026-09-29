#include "ui/memory_target.h"
#include <wincodec.h>
#include <atomic>
#include <cstring>
#include <new>
#include <stdexcept>
#include <utility>

namespace lumashot {
namespace {
// Minimal IWICBitmap exposing existing memory. Direct2D locks it for the
// duration of a draw and writes pixels in place.
class MemoryBitmap final : public IWICBitmap {
public:
    MemoryBitmap(uint32_t* pixels, UINT width, UINT height, UINT stride)
        : pixels_(pixels), width_(width), height_(height), stride_(stride) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if(!out)return E_POINTER;
        if(id==__uuidof(IUnknown)||id==__uuidof(IWICBitmapSource)||id==__uuidof(IWICBitmap)){*out=static_cast<IWICBitmap*>(this);AddRef();return S_OK;}
        *out=nullptr;return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++references_;}
    ULONG STDMETHODCALLTYPE Release() override {const ULONG left=--references_;if(!left)delete this;return left;}
    HRESULT STDMETHODCALLTYPE GetSize(UINT* width, UINT* height) override {
        if(!width||!height)return E_INVALIDARG;*width=width_;*height=height_;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelFormat(WICPixelFormatGUID* format) override {
        if(!format)return E_INVALIDARG;*format=GUID_WICPixelFormat32bppPBGRA;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetResolution(double* x, double* y) override {
        if(!x||!y)return E_INVALIDARG;*x=*y=96;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CopyPalette(IWICPalette*) override {return WINCODEC_ERR_PALETTEUNAVAILABLE;}
    HRESULT STDMETHODCALLTYPE CopyPixels(const WICRect* rect, UINT stride, UINT size, BYTE* buffer) override {
        WICRect all{0,0,static_cast<INT>(width_),static_cast<INT>(height_)};
        const WICRect r=rect?*rect:all;
        if(!buffer||r.X<0||r.Y<0||r.Width<0||r.Height<0||UINT(r.X+r.Width)>width_||UINT(r.Y+r.Height)>height_)return E_INVALIDARG;
        const UINT row=UINT(r.Width)*4;
        if(stride<row||(r.Height&&size<stride*UINT(r.Height-1)+row))return E_INVALIDARG;
        const auto* source=reinterpret_cast<const BYTE*>(pixels_);
        for(INT y=0;y<r.Height;++y)std::memcpy(buffer+size_t(y)*stride,source+(size_t(r.Y+y)*stride_+size_t(r.X))*4,row);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Lock(const WICRect* rect, DWORD, IWICBitmapLock** lock) override;
    HRESULT STDMETHODCALLTYPE SetPalette(IWICPalette*) override {return WINCODEC_ERR_UNSUPPORTEDOPERATION;}
    HRESULT STDMETHODCALLTYPE SetResolution(double, double) override {return WINCODEC_ERR_UNSUPPORTEDOPERATION;}
    uint32_t* pixels_;
    UINT width_, height_, stride_;
private:
    ~MemoryBitmap() = default;
    std::atomic<ULONG> references_{1};
};

class MemoryLock final : public IWICBitmapLock {
public:
    MemoryLock(MemoryBitmap* owner, WICRect rect) : owner_(owner), rect_(rect) {owner_->AddRef();}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if(!out)return E_POINTER;
        if(id==__uuidof(IUnknown)||id==__uuidof(IWICBitmapLock)){*out=static_cast<IWICBitmapLock*>(this);AddRef();return S_OK;}
        *out=nullptr;return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++references_;}
    ULONG STDMETHODCALLTYPE Release() override {const ULONG left=--references_;if(!left)delete this;return left;}
    HRESULT STDMETHODCALLTYPE GetSize(UINT* width, UINT* height) override {
        if(!width||!height)return E_INVALIDARG;*width=UINT(rect_.Width);*height=UINT(rect_.Height);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetStride(UINT* stride) override {
        if(!stride)return E_INVALIDARG;*stride=owner_->stride_*4;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataPointer(UINT* size, WICInProcPointer* data) override {
        if(!size||!data)return E_INVALIDARG;
        const size_t stride=size_t(owner_->stride_)*4;
        *data=reinterpret_cast<BYTE*>(owner_->pixels_)+size_t(rect_.Y)*stride+size_t(rect_.X)*4;
        *size=rect_.Height?UINT(stride*size_t(rect_.Height-1)+size_t(rect_.Width)*4):0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelFormat(WICPixelFormatGUID* format) override {
        if(!format)return E_INVALIDARG;*format=GUID_WICPixelFormat32bppPBGRA;return S_OK;
    }
private:
    ~MemoryLock() {owner_->Release();}
    MemoryBitmap* owner_;
    WICRect rect_;
    std::atomic<ULONG> references_{1};
};

HRESULT MemoryBitmap::Lock(const WICRect* rect, DWORD, IWICBitmapLock** lock) {
    if(!lock)return E_INVALIDARG;*lock=nullptr;
    WICRect all{0,0,static_cast<INT>(width_),static_cast<INT>(height_)};
    const WICRect r=rect?*rect:all;
    if(r.X<0||r.Y<0||r.Width<0||r.Height<0||UINT(r.X+r.Width)>width_||UINT(r.Y+r.Height)>height_)return E_INVALIDARG;
    *lock=new(std::nothrow) MemoryLock(this,r);
    return *lock?S_OK:E_OUTOFMEMORY;
}
}

Microsoft::WRL::ComPtr<ID2D1RenderTarget> CreateMemoryRenderTarget(
    ID2D1Factory* factory, uint32_t* pixels, int width, int height, int stride_pixels, D2D1_ALPHA_MODE alpha) {
    if(!factory||!pixels||width<=0||height<=0||stride_pixels<width)throw std::invalid_argument("Memory render target");
    Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
    bitmap.Attach(new MemoryBitmap(pixels,UINT(width),UINT(height),UINT(stride_pixels)));
    const auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,alpha),96,96);
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
    const HRESULT hr=factory->CreateWicBitmapRenderTarget(bitmap.Get(),&props,&target);
    if(FAILED(hr))throw std::runtime_error("Create memory render target");
    return target;
}
bool ShareBitmap(ID2D1RenderTarget* target, Microsoft::WRL::ComPtr<ID2D1Bitmap>& bitmap){
    if(!bitmap)return true;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> shared;
    const bool ok=SUCCEEDED(target->CreateSharedBitmap(__uuidof(ID2D1Bitmap),bitmap.Get(),nullptr,&shared));
    bitmap=std::move(shared);return ok;
}
}
