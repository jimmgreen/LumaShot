#pragma once
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <wrl/client.h>
#include <memory>

namespace lumashot {
class WindowSurface {
    friend struct StartupRenderTest;
public:
    ~WindowSurface(){Reset();}
    void Create(HWND window,UINT width,UINT height,ID2D1Factory1* factory);
    ID2D1DeviceContext* Target()const{return target_.Get();}
    HANDLE FrameEvent()const{return frame_event_;}
    void FrameReady(){available_=true;}
    bool Acquire(bool hidden);
    HRESULT Present(bool hidden);
    void Reset();
private:
    struct GraphicsDevice;
    std::shared_ptr<GraphicsDevice> graphics_;
    Microsoft::WRL::ComPtr<ID2D1Device> drawing_device_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> target_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> buffer_;
    Microsoft::WRL::ComPtr<IDXGISwapChain2> swap_chain_;
    HANDLE frame_event_{};
    bool available_{};
};
}
