#pragma once
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <stdexcept>
#include <memory>

namespace lumashot {
// Each HWND retains its own visual and swap chain. Windows on the same UI
// thread share the device; a weak cache never keeps it alive after close.
class ClipboardComposition {
    friend struct ClipboardCompositionTest;
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    static void Check(HRESULT result){if(FAILED(result))throw std::runtime_error("Clipboard composition failed");}
    struct Graphics {
        Ptr<ID3D11Device> device;
        Ptr<ID3D11DeviceContext> context;
        Graphics(){
            HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
            if(FAILED(hr))Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
        }
        void Trim(){
            context->ClearState();context->Flush();
            Ptr<IDXGIDevice3> dxgi;if(SUCCEEDED(device.As(&dxgi)))dxgi->Trim();
        }
        ~Graphics(){Trim();}
    };
    static std::shared_ptr<Graphics> Acquire(){
        static thread_local std::weak_ptr<Graphics> cached;
        auto graphics=cached.lock();
        if(!graphics||FAILED(graphics->device->GetDeviceRemovedReason())){
            graphics=std::make_shared<Graphics>();cached=graphics;
        }
        return graphics;
    }
    std::shared_ptr<Graphics> graphics_;
    Ptr<IDXGISwapChain1> chain_;
    Ptr<IDCompositionDevice> composition_;
    Ptr<IDCompositionTarget> target_;
    Ptr<IDCompositionVisual> visual_;
    Ptr<IDCompositionEffectGroup> transition_effect_;
    UINT width_{},height_{};
    void Reset(){
        if(visual_)visual_->SetContent(nullptr);
        if(target_)target_->SetRoot(nullptr);
        if(composition_){composition_->Commit();composition_->WaitForCommitCompletion();}
        chain_.Reset();transition_effect_.Reset();visual_.Reset();target_.Reset();composition_.Reset();
        if(graphics_)graphics_->Trim();
        graphics_.reset();width_=height_=0;
    }
public:
    ClipboardComposition()=default;
    ClipboardComposition(const ClipboardComposition&)=delete;
    ClipboardComposition& operator=(const ClipboardComposition&)=delete;
    ~ClipboardComposition(){Reset();}
    // Call only when going idle. Displayed content remains attached to DWM.
    void TrimIdle(){if(graphics_)graphics_->Trim();}
    void Transition(float offset,float opacity){
        if(!visual_)return;
        if(!transition_effect_){Check(composition_->CreateEffectGroup(&transition_effect_));Check(visual_->SetEffect(transition_effect_.Get()));}
        Check(visual_->SetOffsetX(offset));Check(transition_effect_->SetOpacity(opacity));Check(composition_->Commit());
    }
    void Present(HWND window,UINT width,UINT height,const void* pixels){
        if(graphics_&&FAILED(graphics_->device->GetDeviceRemovedReason()))Reset();
        if(!chain_){
            // A failed partial initialization must not retain an HWND target.
            Reset();graphics_=Acquire();
            try {
                Ptr<IDXGIDevice> dxgi;Check(graphics_->device.As(&dxgi));
                Check(DCompositionCreateDevice(dxgi.Get(),IID_PPV_ARGS(&composition_)));
                Check(composition_->CreateTargetForHwnd(window,TRUE,&target_));
                Check(composition_->CreateVisual(&visual_));Check(target_->SetRoot(visual_.Get()));
                Ptr<IDXGIAdapter> adapter;Check(dxgi->GetAdapter(&adapter));
                Ptr<IDXGIFactory2> factory;Check(adapter->GetParent(IID_PPV_ARGS(&factory)));
                DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=width;desc.Height=height;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
                desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;
                desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;desc.Scaling=DXGI_SCALING_STRETCH;desc.AlphaMode=DXGI_ALPHA_MODE_PREMULTIPLIED;
                Check(factory->CreateSwapChainForComposition(graphics_->device.Get(),&desc,nullptr,&chain_));
                Check(visual_->SetContent(chain_.Get()));width_=width;height_=height;
            }catch(...){Reset();throw;}
        }
        if(width_!=width||height_!=height){Check(chain_->ResizeBuffers(2,width,height,DXGI_FORMAT_UNKNOWN,0));width_=width;height_=height;}
        Ptr<ID3D11Texture2D> buffer;Check(chain_->GetBuffer(0,IID_PPV_ARGS(&buffer)));
        graphics_->context->UpdateSubresource(buffer.Get(),0,nullptr,pixels,width*4,0);
        Check(chain_->Present(1,0));Check(composition_->Commit());
    }
};
}
