#include "ui/render.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <wincodec.h>
#include <psapi.h>
#include <string>
#include <thread>
#include <exception>
#pragma comment(lib,"psapi.lib")
using namespace lumashot;
namespace lumashot {
struct StartupRenderTest {
    static void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Graphics operation failed");}
    static void Memory(const std::string& mode) {
        const auto report=[&](int iteration){
            PROCESS_MEMORY_COUNTERS_EX counters{};counters.cb=sizeof(counters);
            if(!GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),sizeof(counters)))throw std::runtime_error("Memory query failed");
            size_t committed{};MEMORY_BASIC_INFORMATION page{};
            for(uintptr_t address=0;VirtualQuery(reinterpret_cast<void*>(address),&page,sizeof(page));) {
                if(page.State==MEM_COMMIT&&page.Type==MEM_PRIVATE)committed+=page.RegionSize;
                const auto next=reinterpret_cast<uintptr_t>(page.BaseAddress)+page.RegionSize;if(next<=address)break;address=next;
            }
            std::cout<<mode<<" iteration="<<iteration<<" private_mib="<<double(counters.PrivateUsage)/(1024*1024)
                <<" committed_private_mib="<<double(committed)/(1024*1024)<<"\n";
            return counters.PrivateUsage;
        };
        std::unique_ptr<Renderer> reused;
        if(mode=="demo-reuse")reused=std::make_unique<Renderer>();
        report(0);
        SIZE_T first_use{};
        for(int i=1;i<=12;++i) {
            {
                struct Window {HWND value{};~Window(){if(value)DestroyWindow(value);}};
                Window window{CreateWindowExW(0,L"STATIC",L"Synthetic memory fixture",WS_POPUP,0,0,320,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};
                if(!window.value)throw std::runtime_error("Memory fixture window failed");
                const RECT bounds{0,0,320,200};
                if(mode=="surface") {
                    ComPtr<ID2D1Factory1> factory;Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf()));
                    WindowSurface surface;surface.Create(window.value,320,200,factory.Get());Draw(surface);
                } else {
                    Renderer renderer;
                    if(mode=="context") {
                        auto desc=LumaText::Descriptor<lt_context_desc>();desc.dwrite_factory=renderer.text_factory_.Get();desc.cpu_cache_limit_bytes=16ull*1024*1024;
                        LumaText::Context context;
                        if(lt_context_create(&desc,context.put())!=LT_OK)throw std::runtime_error("Context fixture failed");
                    }
                    else if(mode=="demo-thread") {
                        std::exception_ptr error;
                        std::thread worker([&]{
                            const auto hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
                            try{Check(hr);Renderer exported;(void)exported.Demo();}catch(...){error=std::current_exception();}
                            if(SUCCEEDED(hr))CoUninitialize();
                        });worker.join();
                        if(error)std::rethrow_exception(error);
                    }
                    else if(mode=="demo-reuse")(void)reused->Demo();
                    else if(mode=="prepare")renderer.Prepare(window.value,bounds);
                    else if(mode=="demo")(void)renderer.Demo();
                    else {
                        const auto frame=MakeFrame(bounds,0xff345678),acrylic=BlurBackdrop(frame);
                        ViewState state;state.external_magnifier=true;
                        if(mode=="text")state.hint=L"Screenshot fixture 中文文字";
                        if(!renderer.Paint(window.value,bounds,frame,acrylic,Document{},std::nullopt,state))throw std::runtime_error("Memory fixture paint failed");
                    }
                }
            }
            const auto memory=report(i);
            if(i==1)first_use=memory;
            else if(memory>first_use+16*1024*1024)throw std::runtime_error("Repeated rendering retains unbounded private memory");
        }
    }
    static void Regions() {
        Renderer renderer;
        ComPtr<IWICImagingFactory> wic;
        Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)));
        auto frame=MakeFrame({-31,-19,42,38});
        for(size_t i=0;i<frame.pixels.size();++i)frame.pixels[i]=0xff000000|static_cast<uint32_t>((i*2654435761u)&0xffffff);
        for(const RECT region:{RECT{-21,-12,12,8},RECT{3,5,29,31},RECT{-45,-30,-19,-3},RECT{30,26,60,55}}) {
            const auto expected=Crop(frame,region);
            ComPtr<IWICBitmap> destination;
            Check(wic->CreateBitmap(expected.Width(),expected.Height(),GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&destination));
            renderer.target_.Reset();
            Check(renderer.factory_->CreateWicBitmapRenderTarget(destination.Get(),D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&renderer.target_));
            auto bitmap=renderer.BitmapRegion(frame,region);
            renderer.target_->BeginDraw();
            renderer.target_->DrawBitmap(bitmap.Get(),nullptr,1,D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
            Check(renderer.target_->EndDraw());
            auto actual=MakeFrame(expected.bounds);
            Check(destination->CopyPixels(nullptr,actual.Width()*4,static_cast<UINT>(actual.pixels.size()*4),reinterpret_cast<BYTE*>(actual.pixels.data())));
            if(actual.pixels!=expected.pixels)throw std::runtime_error("Region stride/coordinate mismatch");
        }
        std::cout<<"Region uploads match Crop with negative origins and clipped bounds\n";
    }
    static ComPtr<IUnknown> Identity(WindowSurface& surface) {
        ComPtr<ID2D1Image> image;surface.Target()->GetTarget(&image);
        ComPtr<ID2D1Bitmap1> bitmap;Check(image.As(&bitmap));
        ComPtr<IDXGISurface> buffer;Check(bitmap->GetSurface(&buffer));
        ComPtr<IUnknown> identity;Check(buffer->GetDevice(IID_PPV_ARGS(&identity)));return identity;
    }
    static void Draw(WindowSurface& surface) {
        if(!surface.Acquire(true))throw std::runtime_error("Hidden surface not ready");
        surface.Target()->BeginDraw();surface.Target()->Clear(D2D1::ColorF(0x234567));
        Check(surface.Target()->EndDraw());Check(surface.Present(true));
    }
    static void Surfaces() {
        struct Window {HWND value{};~Window(){if(value)DestroyWindow(value);}};
        Window first{CreateWindowExW(0,L"STATIC",L"Synthetic surface 1",WS_POPUP,0,0,64,48,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};
        Window second{CreateWindowExW(0,L"STATIC",L"Synthetic surface 2",WS_POPUP,0,0,64,48,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};
        if(!first.value||!second.value)throw std::runtime_error("Hidden fixture creation failed");
        ComPtr<ID2D1Factory1> factory;Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf()));
        WindowSurface a,b;
        a.Create(first.value,64,48,factory.Get());b.Create(second.value,64,48,factory.Get());
        auto first_device=Identity(a),second_device=Identity(b);
        if(first_device.Get()!=second_device.Get())throw std::runtime_error("Live surfaces did not share D3D device");
        std::weak_ptr<WindowSurface::GraphicsDevice> lifetime=a.graphics_;
        Draw(a);Draw(b);first_device.Reset();second_device.Reset();
        a.Reset();Draw(b);
        if(lifetime.expired())throw std::runtime_error("Device released before final surface");
        b.Reset();
        if(!lifetime.expired())throw std::runtime_error("Device retained after all surfaces reset");
        a.Create(first.value,64,48,factory.Get());Draw(a);a.Reset();
        {
            Renderer prepared;
            const RECT bounds{-19,-11,45,37};
            prepared.Prepare(first.value,bounds);
            auto* target=prepared.window_surface_.Target();
            prepared.Prepare(first.value,bounds);
            if(!target||prepared.window_surface_.Target()!=target||prepared.screen_bitmap_||prepared.acrylic_bitmap_)
                throw std::runtime_error("Prepare must be idempotent without image allocation");
            const auto frame=MakeFrame(bounds,0xff345678),acrylic=BlurBackdrop(frame);
            ViewState state;state.external_magnifier=true;
            if(!prepared.Paint(first.value,bounds,frame,acrylic,Document{},std::nullopt,state)||
                !prepared.screen_bitmap_||!prepared.acrylic_bitmap_||prepared.window_surface_.Target()!=target)
                throw std::runtime_error("Paint did not upload into prepared surface");
        }
        std::cout<<"Prepare creates no images, is idempotent, and Paint reuses its surface\n";
        std::cout<<"Hidden surfaces share device, survive peer destruction, release and recreate\n";
    }
};
}
static Frame Reference(const Frame& frame) {
    const int w=std::max(1,frame.Width()/4),h=std::max(1,frame.Height()/4);
    Frame reduced=MakeFrame({0,0,w,h});
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)
        reduced.pixels[static_cast<size_t>(y)*w+x]=frame.pixels[static_cast<size_t>(y*4)*frame.Width()+x*4];
    std::vector<uint32_t> scratch(reduced.pixels.size());
    for(int pass=0;pass<2;++pass) {
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            unsigned r{},g{},b{};
            for(int d=-4;d<=4;++d) {
                const int px=pass==0?std::clamp(x+d,0,w-1):x;
                const int py=pass==1?std::clamp(y+d,0,h-1):y;
                const auto c=reduced.pixels[static_cast<size_t>(py)*w+px];
                r+=(c>>16)&255;g+=(c>>8)&255;b+=c&255;
            }
            scratch[static_cast<size_t>(y)*w+x]=0xff000000|((r/9)<<16)|((g/9)<<8)|(b/9);
        }
        reduced.pixels.swap(scratch);
    }
    return reduced;
}
int main(int argc,char** argv) {
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 2;
    struct Apartment {~Apartment(){CoUninitialize();}} apartment;
    try {
        if(argc==2){StartupRenderTest::Memory(argv[1]);return 0;}
        StartupRenderTest::Regions();StartupRenderTest::Surfaces();
        uint32_t random=12345;
        for(const auto size:{POINT{1,1},POINT{3,7},POINT{4,4},POINT{17,31},POINT{73,91},POINT{1920,1080},POINT{3840,2160}}) {
            auto frame=MakeFrame({-311,-89,size.x-311,size.y-89});
            for(auto& pixel:frame.pixels){random=random*1664525u+1013904223u;pixel=random;}
            const auto expected=Reference(frame),actual=BlurBackdrop(frame);
            if(expected.pixels!=actual.pixels||!EqualRect(&expected.bounds,&actual.bounds))throw std::runtime_error("Backdrop pixels changed");
            if(size.x<1920)continue;
            const auto bench=[&](auto function) {
                const auto start=std::chrono::steady_clock::now();
                size_t checksum{};
                for(int i=0;i<20;++i){const auto result=function(frame);checksum+=result.pixels[result.pixels.size()/2];}
                std::cout<<" checksum="<<checksum;
                return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/20;
            };
            std::cout<<"\n"<<size.x<<"x"<<size.y;
            const double before=bench(Reference),after=bench(BlurBackdrop);
            std::cout<<" reference_ms="<<before<<" optimized_ms="<<after<<"\n";
        }
        std::cout<<"Backdrop exact-pixel tests passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
