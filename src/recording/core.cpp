#include "recording/core.h"
#include "recording/encoder.h"
#include "recording/audio.h"
#include "recording/latest_frame.h"
#include "app/diagnostics.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <avrt.h>
#include <d3d10.h>
#include <algorithm>
#include <cmath>
namespace lumashot::recording {
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
struct Event {HANDLE value{CreateEventW(nullptr,FALSE,FALSE,nullptr)};~Event(){if(value)CloseHandle(value);}};
// Frame pacing for the capture thread. WaitForSingleObject timeouts round to the
// ~15.6 ms system tick, and Windows coalesces timers and applies EcoQoS to
// processes that are not in the foreground, which a recorder almost never is:
// frames then arrive 60-250 ms late and are skipped. A high-resolution waitable
// timer wakes on time, MMCSS "Capture" schedules the thread as media capture,
// and execution-speed throttling is turned off for this thread only.
class FramePacer {
    HANDLE timer_{},task_{};
public:
    FramePacer(){
        timer_=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
        if(!timer_)timer_=CreateWaitableTimerExW(nullptr,nullptr,0,TIMER_ALL_ACCESS);
        DWORD index=0;task_=AvSetMmThreadCharacteristicsW(L"Capture",&index);
        THREAD_POWER_THROTTLING_STATE state{THREAD_POWER_THROTTLING_CURRENT_VERSION,THREAD_POWER_THROTTLING_EXECUTION_SPEED,0};
        SetThreadInformation(GetCurrentThread(),ThreadPowerThrottling,&state,sizeof(state));
    }
    FramePacer(const FramePacer&)=delete;FramePacer& operator=(const FramePacer&)=delete;
    ~FramePacer(){if(task_)AvRevertMmThreadCharacteristics(task_);if(timer_)CloseHandle(timer_);}
    // Waits up to `delay` (100 ns units, capped at 50 ms) or until `wake` is signaled.
    void Wait(HANDLE wake,long long delay){
        delay=std::clamp(delay,0LL,500000LL);
        LARGE_INTEGER due{};due.QuadPart=-delay;
        if(!timer_||!SetWaitableTimer(timer_,&due,0,nullptr,nullptr,FALSE)){WaitForSingleObject(wake,DWORD(delay/10000+1));return;}
        const HANDLE handles[]{wake,timer_};WaitForMultipleObjects(2,handles,FALSE,100);
    }
};
void Session::Start(Options options,std::filesystem::path file,std::function<void(Status)> report){
    if(thread_.joinable())throw std::runtime_error("Recording already started");stop_=false;pause_=false;
    thread_=std::jthread([this,options,file=std::move(file),report=std::move(report)]{
        std::wstring stage=L"初始化";bool initialized=false,mf=false;Status status;status.state=State::Starting;report(status);
        try{
            winrt::init_apartment(winrt::apartment_type::multi_threaded);initialized=true;Check(MFStartup(MF_VERSION),"Media Foundation startup");mf=true;
            ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
            Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_VIDEO_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"D3D11 device");
            ComPtr<ID3D10Multithread> protection;if(SUCCEEDED(context.As(&protection)))protection->SetMultithreadProtected(TRUE);
            GraphicsCaptureItem item{nullptr};Direct3D11CaptureFramePool pool{nullptr};GraphicsCaptureSession capture{nullptr};winrt::event_token token{};
            auto event=std::make_shared<Event>();if(!event->value)throw std::runtime_error("Capture event failed");
            struct CaptureClose {Direct3D11CaptureFramePool& pool;GraphicsCaptureSession& capture;winrt::event_token& token;~CaptureClose(){try{if(pool)pool.FrameArrived(token);if(capture)capture.Close();if(pool)pool.Close();}catch(...){}}} close{pool,capture,token};
            winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice capture_device{nullptr};
            const auto create_capture=[&]{pool=Direct3D11CaptureFramePool::CreateFreeThreaded(capture_device,DirectXPixelFormat::B8G8R8A8UIntNormalized,2,item.Size());token=pool.FrameArrived([event](auto const&,auto const&){SetEvent(event->value);});capture=pool.CreateCaptureSession(item);capture.IsCursorCaptureEnabled(options.cursor);};
            SIZE input=options.synthetic_size;RECT crop{0,0,input.cx,input.cy};
            if(!options.synthetic){
                stage=L"WGC 支持检测";if(!GraphicsCaptureSession::IsSupported())throw std::runtime_error("Windows Graphics Capture unavailable");
                stage=L"WGC 目标";const auto interop=winrt::get_activation_factory<GraphicsCaptureItem,IGraphicsCaptureItemInterop>();
                if(options.target)Check(interop->CreateForWindow(options.target,winrt::guid_of<GraphicsCaptureItem>(),winrt::put_abi(item)),"Capture window");
                else Check(interop->CreateForMonitor(options.monitor,winrt::guid_of<GraphicsCaptureItem>(),winrt::put_abi(item)),"Capture monitor");
                const auto size=item.Size();input={size.Width,size.Height};crop={0,0,input.cx,input.cy};
                if(!options.target){MONITORINFO monitor{sizeof(monitor)};if(!GetMonitorInfoW(options.monitor,&monitor))throw std::runtime_error("Monitor disconnected");crop=options.region;OffsetRect(&crop,-monitor.rcMonitor.left,-monitor.rcMonitor.top);crop.left=std::clamp(crop.left,0L,input.cx);crop.top=std::clamp(crop.top,0L,input.cy);crop.right=std::clamp(crop.right,0L,input.cx);crop.bottom=std::clamp(crop.bottom,0L,input.cy);}
                ComPtr<IDXGIDevice> dxgi;Check(device.As(&dxgi),"DXGI capture device");winrt::com_ptr<IInspectable> inspectable;Check(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(),inspectable.put()),"WinRT D3D11 device");
                stage=L"WGC 帧池";capture_device=inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
                stage=L"WGC 会话";create_capture();
            }
            const SIZE output=OutputSize(crop.right-crop.left,crop.bottom-crop.top,options.width);
            std::unique_ptr<Audio> audio;if(!options.synthetic&&(options.system_audio||options.microphone))audio=std::make_unique<Audio>(options.system_audio,options.microphone);
            const bool synthetic_audio=options.synthetic&&options.system_audio;
            Encoder encoder(device.Get(),file,input,output,options.fps,options.software,audio!=nullptr||synthetic_audio,options.quality);
            ComPtr<ID3D11Texture2D> latest;D3D11_TEXTURE2D_DESC d{};d.Width=input.cx;d.Height=input.cy;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.BindFlags=D3D11_BIND_RENDER_TARGET;Check(device->CreateTexture2D(&d,nullptr,&latest),"Capture snapshot texture");
            ComPtr<ID3D11RenderTargetView> synthetic_view;if(options.synthetic)Check(device->CreateRenderTargetView(latest.Get(),nullptr,&synthetic_view),"Synthetic render target");
            if(stop_){encoder.Finish();throw std::runtime_error("Recording canceled before start");}
            stage=L"WGC 启动";if(capture)capture.StartCapture();if(audio)audio->Pause(false);
            FramePacer pacer;
            Clock clock;bool paused=false,received=options.synthetic;long long next=0,last_report=-10000000,audio_frames=0;const long long step=10000000/options.fps;
            status.state=State::Recording;report(status);
            while(!stop_){
                if(pause_!=paused){stage=L"WGC 暂停恢复";paused=pause_;clock.Pause(paused);if(audio)audio->Pause(paused);if(capture){capture.Close();capture=nullptr;}if(pool){pool.FrameArrived(token);pool.Close();pool=nullptr;}if(!paused&&item){create_capture();capture.StartCapture();}status.state=paused?State::Paused:State::Recording;report(status);}
                if(paused){WaitForSingleObject(event->value,50);continue;}
                if(options.target&&!IsWindow(options.target))break;
                if(options.target&&IsIconic(options.target)){pause_=true;continue;}
                if(clock.Now()<next){pacer.Wait(event->value,next-clock.Now());continue;}
                stage=L"WGC 读取帧";if(pool){
                    ConsumeLatestFrame([&]{return pool.TryGetNextFrame();},[&](const Direct3D11CaptureFrame& frame){
                        const auto size=frame.ContentSize();
                        if(size.Width!=input.cx||size.Height!=input.cy)throw std::runtime_error("Capture target resized. Save this recording and start again with the new size.");
                    },[&](const Direct3D11CaptureFrame& newest){
                        // Keep the capture frame alive until its texture copy is submitted.
                        auto access=newest.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
                        ComPtr<ID3D11Texture2D> source;Check(access->GetInterface(IID_PPV_ARGS(&source)),"Capture texture");
                        context->CopyResource(latest.Get(),source.Get());received=true;
                    });
                }
                const auto now=clock.Now();if(!received){if(now>50000000)throw std::runtime_error("No capture frames received");WaitForSingleObject(event->value,30);continue;}
                if(now<next){pacer.Wait(event->value,next-now);continue;}
                if(options.synthetic){const float color[]={float(status.frames%30)/30.f,.35f,.75f,1};context->ClearRenderTargetView(synthetic_view.Get(),color);}
                // One frame in flight on this thread; skip overdue presentation
                // times instead of accumulating frames when an encoder is slow.
                if(now-next>step*2){const auto skipped=(now-next)/step;status.dropped+=static_cast<unsigned>(skipped);next+=skipped*step;Diagnostics::Get().Add("record_drop",Diagnostics::Now(),skipped);}
                {TraceScope encode("record_encode",status.frames);encoder.Frame(latest.Get(),crop,next,step);}
                {TraceScope audio_trace("record_audio");if(audio||synthetic_audio){const auto end=(next+step)*48000/10000000;const auto count=static_cast<size_t>(end-audio_frames);if(count>24000)throw std::runtime_error("Encoder cannot keep up with audio");std::vector<short> samples;if(audio)samples=audio->Read(count);else{samples.resize(count*2);for(size_t i=0;i<count;++i)samples[i*2]=samples[i*2+1]=short(8000*std::sin((audio_frames+static_cast<long long>(i))*6.283185307179586*440/48000));}encoder.AudioFrame(samples.data(),count,audio_frames);audio_frames=end;}}
                ++status.frames;next+=step;status.time=next;
                if(now-last_report>=10000000){report(status);last_report=now;std::error_code error;const auto space=std::filesystem::space(file.parent_path(),error);if(!error&&space.available<64*1024*1024)throw std::runtime_error("Insufficient free space");}
                if(options.synthetic&&status.frames>=static_cast<unsigned>(options.synthetic_frames))break;
                if(options.duration_limit&&status.frames>=static_cast<unsigned>(options.duration_limit*options.fps/10000000))break;
            }
            if(capture){capture.Close();capture=nullptr;}if(audio)audio->Pause(true);status.state=State::Finishing;report(status);encoder.Finish();status.state=State::Preview;report(status);
        }catch(const winrt::hresult_error& error){status.state=State::Failed;status.detail=L"HRESULT "+std::to_wstring(static_cast<long>(error.code()))+L" / "+stage+L": "+error.message().c_str();report(status);}
        catch(const std::exception& error){status.state=State::Failed;const std::string text=error.what();status.detail.assign(text.begin(),text.end());report(status);}
        if(mf)MFShutdown();if(initialized)winrt::uninit_apartment();
    });
}
}
