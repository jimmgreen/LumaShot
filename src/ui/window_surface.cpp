#include "ui/window_surface.h"
#include <system_error>

namespace lumashot {
using Microsoft::WRL::ComPtr;
static void CheckSurface(HRESULT hr){if(FAILED(hr))throw std::system_error(static_cast<int>(hr),std::system_category(),"Capture display");}
struct WindowSurface::GraphicsDevice {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ~GraphicsDevice(){
        if(context){context->ClearState();context->Flush();}
        // The final overlay is closed: discard deferred driver allocations only
        // after all swap chains and Direct2D resources have been released.
        ComPtr<IDXGIDevice3> dxgi;
        if(device&&SUCCEEDED(device.As(&dxgi)))dxgi->Trim();
    }
};
void WindowSurface::Create(HWND window,UINT width,UINT height,ID2D1Factory1* factory) {
    Reset();
    try {
        // Share only live overlays on this thread. A weak cache retains no GPU
        // device or screenshot allocation after the last overlay is destroyed.
        static thread_local std::weak_ptr<GraphicsDevice> live_device;
        graphics_=live_device.lock();
        if(graphics_&&FAILED(graphics_->device->GetDeviceRemovedReason()))graphics_.reset();
        if(!graphics_) {
            graphics_=std::make_shared<GraphicsDevice>();
            HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr,0,D3D11_SDK_VERSION,&graphics_->device,nullptr,&graphics_->context);
            if(FAILED(hr))CheckSurface(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr,0,D3D11_SDK_VERSION,&graphics_->device,nullptr,&graphics_->context));
            live_device=graphics_;
        }
        ComPtr<IDXGIDevice> dxgi;CheckSurface(graphics_->device.As(&dxgi));
        CheckSurface(factory->CreateDevice(dxgi.Get(),&drawing_device_));
        CheckSurface(drawing_device_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&target_));
        ComPtr<IDXGIAdapter> adapter;CheckSurface(dxgi->GetAdapter(&adapter));
        ComPtr<IDXGIFactory2> display;CheckSurface(adapter->GetParent(IID_PPV_ARGS(&display)));
        DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=width;desc.Height=height;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;
        desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;desc.Scaling=DXGI_SCALING_STRETCH;
        desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;desc.Flags=DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        ComPtr<IDXGISwapChain1> chain;CheckSurface(display->CreateSwapChainForHwnd(graphics_->device.Get(),window,&desc,nullptr,nullptr,&chain));
        CheckSurface(chain.As(&swap_chain_));CheckSurface(swap_chain_->SetMaximumFrameLatency(1));
        CheckSurface(display->MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER));
        frame_event_=swap_chain_->GetFrameLatencyWaitableObject();if(!frame_event_)CheckSurface(E_FAIL);
        ComPtr<IDXGISurface> surface;CheckSurface(swap_chain_->GetBuffer(0,IID_PPV_ARGS(&surface)));
        auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
        CheckSurface(target_->CreateBitmapFromDxgiSurface(surface.Get(),&properties,&buffer_));
        target_->SetTarget(buffer_.Get());target_->SetDpi(96,96);
    }catch(...){Reset();throw;}
}
bool WindowSurface::Acquire(bool hidden) {
    // The visible UI never waits for the GPU. Application waits for this event
    // alongside input only when a newer frame is pending, with no idle loop.
    if(!available_)available_=WaitForSingleObject(frame_event_,hidden?1000:0)==WAIT_OBJECT_0;
    if(available_)target_->SetTarget(buffer_.Get());
    return available_;
}
HRESULT WindowSurface::Present(bool hidden) {
    if(hidden)return S_OK; // Prepare resources without exposing an unfinished frame.
    const HRESULT hr=swap_chain_->Present(1,0);
    available_=false;
    return hr;
}
void WindowSurface::Reset() {
    if(target_)target_->SetTarget(nullptr);
    buffer_.Reset();target_.Reset();
    if(drawing_device_)drawing_device_->ClearResources(0);
    drawing_device_.Reset();swap_chain_.Reset();
    if(frame_event_)CloseHandle(frame_event_);frame_event_=nullptr;available_=false;
    graphics_.reset();
}
}
