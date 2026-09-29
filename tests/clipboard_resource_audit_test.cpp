// Isolated synthetic audit. Never subscribes to, reads or writes the OS clipboard.
#include "clipboard_preview_probe.h"
#include "../src/clipboard/panel.cpp"
#include "export/png.h"
#include <algorithm>
#include <psapi.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>

namespace lumashot {
struct ClipboardCompositionTest {
    static bool Shared(const ClipboardComposition& a,const ClipboardComposition& b){return a.graphics_&&a.graphics_==b.graphics_&&a.chain_.Get()!=b.chain_.Get()&&a.target_.Get()!=b.target_.Get();}
    static std::weak_ptr<void> Lifetime(const ClipboardComposition& a){return a.graphics_;}
};
namespace {
using Clock = std::chrono::steady_clock;
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
double Ms(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
struct Memory {SIZE_T bytes{},working{};DWORD handles{},gdi{},user{};};
Memory ReadMemory(){
    PROCESS_MEMORY_COUNTERS_EX m{};m.cb=sizeof(m);
    Require(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m))!=FALSE,"memory sample");
    Memory r{m.PrivateUsage,m.WorkingSetSize};GetProcessHandleCount(GetCurrentProcess(),&r.handles);
    r.gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);r.user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);return r;
}
Memory Sample(const char* stage,int cycle=0){
    const auto r=ReadMemory();std::cout<<"MEMORY stage="<<stage<<" cycle="<<cycle<<" private="<<r.bytes<<" working="<<r.working
        <<" handles="<<r.handles<<" gdi="<<r.gdi<<" user="<<r.user<<std::endl;return r;
}
void HeapSnapshot(const char* stage){
    HANDLE heaps[256]{};const DWORD count=GetProcessHeaps(256,heaps);
    size_t total_busy=0,total_free=0,total_committed=0;
    struct Large {size_t bytes;uintptr_t prefix;};Large large[128]{};size_t large_count=0;
    for(DWORD i=0;i<std::min<DWORD>(count,256);++i){
        size_t busy=0,free=0,committed=0;DWORD error=0;
        if(!HeapLock(heaps[i]))continue;
        PROCESS_HEAP_ENTRY entry{};
        while(HeapWalk(heaps[i],&entry)){
            if(entry.wFlags&PROCESS_HEAP_REGION)committed+=entry.Region.dwCommittedSize;
            else if(entry.wFlags&PROCESS_HEAP_ENTRY_BUSY){
                busy+=entry.cbData;
                if(entry.cbData>=256*1024&&large_count<128){large[large_count].bytes=entry.cbData;std::memcpy(&large[large_count].prefix,entry.lpData,sizeof(uintptr_t));++large_count;}
            }
            else if(!(entry.wFlags&PROCESS_HEAP_UNCOMMITTED_RANGE))free+=entry.cbData;
        }
        error=GetLastError();HeapUnlock(heaps[i]);
        total_busy+=busy;total_free+=free;total_committed+=committed;
        if(busy+free>1024*1024)std::cout<<"HEAP stage="<<stage<<" index="<<i<<" busy="<<busy<<" free="<<free<<" committed="<<committed<<" error="<<error<<std::endl;
    }
    std::sort(large,large+large_count,[](const Large& a,const Large& b){return a.bytes>b.bytes;});
    for(size_t i=0;i<std::min<size_t>(large_count,20);++i)std::cout<<"HEAP_LARGE stage="<<stage<<" bytes="<<large[i].bytes<<" prefix="<<std::hex<<large[i].prefix<<std::dec<<std::endl;
    std::cout<<"HEAP_TOTAL stage="<<stage<<" heaps="<<count<<" busy="<<total_busy<<" free="<<total_free<<" committed="<<total_committed<<std::endl;
}
void Times(const char* name,std::vector<double> v){
    Require(!v.empty(),"timing samples");std::sort(v.begin(),v.end());
    std::cout<<"TIMING "<<name<<" n="<<v.size()<<" p50_ms="<<v[v.size()/2]<<" p95_ms="<<v[(v.size()-1)*95/100]<<" max_ms="<<v.back()<<std::endl;
}
void Pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE))if(m.message!=WM_QUIT){TranslateMessage(&m);DispatchMessageW(&m);}}
uint64_t CpuTime(){FILETIME creation{},exit{},kernel{},user{};GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user);
    ULARGE_INTEGER k{},u{};k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;return k.QuadPart+u.QuadPart;}
void Idle(const char* stage){Pump();const auto before=CpuTime();const auto start=Clock::now();Sleep(1000);Pump();
    std::cout<<"IDLE stage="<<stage<<" elapsed_ms="<<Ms(start)<<" process_cpu_ms="<<(CpuTime()-before)/10000.0<<std::endl;}
clipboard::Entry Text(int i){
    clipboard::Entry e;e.kind=clipboard::Kind::Text;e.text=std::wstring(128*1024,L'x')+std::to_wstring(i);
    const auto* b=reinterpret_cast<const unsigned char*>(e.text.c_str());e.formats.push_back({CF_UNICODETEXT,{b,b+(e.text.size()+1)*sizeof(wchar_t)}});return e;
}
clipboard::Entry Dib(int i){
    clipboard::Entry e;e.kind=clipboard::Kind::Image;e.text=L"synthetic DIB "+std::to_wstring(i);
    BITMAPINFOHEADER h{};h.biSize=sizeof(h);h.biWidth=512;h.biHeight=-512;h.biPlanes=1;h.biBitCount=32;
    std::vector<unsigned char> bytes(sizeof(h)+512*512*4);std::memcpy(bytes.data(),&h,sizeof(h));
    const uint32_t color=0xff4080a0u+static_cast<uint32_t>(i);
    for(size_t j=sizeof(h);j<bytes.size();j+=4)std::memcpy(bytes.data()+j,&color,4);
    e.formats.push_back({CF_DIB,std::move(bytes)});return e;
}
clipboard::Entry Png(int i,const std::filesystem::path& path){
    {auto frame=MakeFrame({0,0,3840,2160});for(int y=0;y<2160;++y)for(int x=0;x<3840;++x)
        frame.pixels[static_cast<size_t>(y)*3840+x]=0xff000000u|((static_cast<uint32_t>(x/16+i)&255)<<16)|((static_cast<uint32_t>(y/8)&255)<<8)|0x80u;
        SavePng(frame,path);}
    std::ifstream file(path,std::ios::binary);std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),{});
    clipboard::Entry e;e.kind=clipboard::Kind::Image;e.text=L"synthetic 4K PNG "+std::to_wstring(i);e.formats.push_back({RegisterClipboardFormatW(L"PNG"),std::move(bytes)});return e;
}
}
struct ClipboardPanelTest {
static int Run(int cycles){
    const auto root=std::filesystem::temp_directory_path()/(L"lumashot-resource-audit-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove_all(path,ec);}}cleanup{root};
    ClipboardPanel panel(nullptr,[]{});auto& p=*panel.impl_;p.test_mode=true;
    std::cout<<std::fixed<<std::setprecision(3);Sample("cold");
    Require(p.Enable(true,false),"enable isolated panel");p.disk=std::make_unique<clipboard::SessionStore>(root/L"store");Require(p.disk->WaitIdle(),"worker initialization");
    const auto save=[&](clipboard::Entry e){Require(p.disk->Save(std::move(e)),"queue fixture");Require(p.disk->WaitIdle(),"save fixture");p.DrainStore();};
    const auto start_save=Clock::now();
    for(int i=0;i<80;++i)save(Text(i));
    for(int i=0;i<16;++i)save(Dib(i));
    for(int i=0;i<4;++i)save(Png(i,root/(L"fixture-"+std::to_wstring(i)+L".png")));
    Require(p.history.entries.size()==100,"100 history entries accepted");
    size_t metadata=0;std::vector<uint64_t> png_ids;
    for(const auto& e:p.history.entries){Require(e.formats.empty()&&e.payload,"only metadata resident");metadata+=e.text.capacity()*sizeof(wchar_t);if(e.text.starts_with(L"synthetic 4K"))png_ids.push_back(e.id);}
    std::cout<<"HISTORY entries="<<p.history.entries.size()<<" logical_bytes="<<p.history.Bytes()<<" text_capacity_bytes="<<metadata<<" fill_ms="<<Ms(start_save)<<std::endl;
    Sample("history100");
    const auto settle=[&](){Require(p.disk->WaitIdle(),"worker settles");p.DrainStore();};
    const auto open=[&](){p.expanded=true;p.scale=1;p.Filter();p.Place();p.Present();settle();p.Present();Pump();};
    open();Sample("panel_open");Idle("panel_open");
    if(GetEnvironmentVariableW(L"LUMASHOT_PROFILE_HEAPS",nullptr,0))HeapSnapshot("panel_open");
    std::vector<double> keys;for(int i=0;i<1000;++i){const auto t=Clock::now();p.Key(i%2?VK_UP:VK_DOWN);keys.push_back(Ms(t));}Times("navigation_dispatch_only",keys);
    p.selected=png_ids.front();p.ShowPreview();settle();Require(clipboard::PreviewWindowTest::HasImage(p.preview),"4K preview loads");
    {auto frame=clipboard::PreviewWindowTest::Snapshot(p.preview);Require(!frame.pixels.empty(),"preview paints");}
    UpdateWindow(clipboard::PreviewWindowTest::Window(p.preview));Pump();
    const auto preview_composition=clipboard::PreviewWindowTest::Composition(p.preview);
    Require(preview_composition&&ClipboardCompositionTest::Shared(*p.composition,*preview_composition),"list and preview share one graphics device but own separate surfaces and targets");
    const auto device_lifetime=ClipboardCompositionTest::Lifetime(*p.composition);
    Sample("preview_open");Idle("preview_open");
    // DWM/DirectComposition initializes driver and theme resources asynchronously.
    // Warm all cached images before measuring steady-state growth; cold/open
    // samples above retain visibility into that initial allocation cost.
    for(int i=0;i<40;++i){p.Action(30,png_ids[static_cast<size_t>(i)%png_ids.size()]);settle();clipboard::PreviewWindowTest::Snapshot(p.preview);Pump();}
    DwmFlush();Pump();
    // Measure replacement while the preview is open, not cold process startup.
    std::vector<double> dispatch,ready,paint;int window_recreates=0,renderer_recreates=0;
    Memory warm{},last{};HWND previous_window=clipboard::PreviewWindowTest::Window(p.preview);
    const void* previous_renderer=clipboard::PreviewWindowTest::Renderer(p.preview);
    const auto measure_peak_start=ReadMemory().bytes;std::atomic<size_t> peak{measure_peak_start};
    std::jthread sampler([&](std::stop_token token){while(!token.stop_requested()){const auto now=ReadMemory().bytes;auto prior=peak.load();while(now>prior&&!peak.compare_exchange_weak(prior,now)){}Sleep(5);}});
    for(int i=1;i<=cycles;++i){
        const auto t=Clock::now();p.Action(30,png_ids[static_cast<size_t>(i)%png_ids.size()]);dispatch.push_back(Ms(t));settle();ready.push_back(Ms(t));
        Require(clipboard::PreviewWindowTest::HasImage(p.preview),"latest preview has image");
        const auto r=Clock::now();{auto frame=clipboard::PreviewWindowTest::Snapshot(p.preview);Require(!frame.pixels.empty(),"render switched preview");}paint.push_back(Ms(r));
        if(clipboard::PreviewWindowTest::Window(p.preview)!=previous_window)++window_recreates;
        if(clipboard::PreviewWindowTest::Renderer(p.preview)!=previous_renderer)++renderer_recreates;
        previous_window=clipboard::PreviewWindowTest::Window(p.preview);previous_renderer=clipboard::PreviewWindowTest::Renderer(p.preview);
        Pump();if(i==10)warm=ReadMemory();
        if(i==1||i==10||i==50||i==100||i==cycles)last=Sample("preview_switch",i);
    }
    sampler.request_stop();sampler.join();
    Times("preview_switch_dispatch",dispatch);Times("preview_switch_ready_including_io_decode",ready);Times("preview_render_and_snapshot",paint);
    std::cout<<"PREVIEW window_recreates="<<window_recreates<<" renderer_address_changes="<<renderer_recreates<<" sampled_peak_private="<<peak.load()<<" peak_phase_start_private="<<measure_peak_start<<std::endl;
    Require(window_recreates==0&&renderer_recreates==0,"switching preview reuses the native window and renderer");
    Require(last.bytes<=warm.bytes+16*1024*1024,"preview switch private memory stays bounded after warmup");
    Require(last.gdi<=warm.gdi+2&&last.user<=warm.user+2&&last.handles<=warm.handles+8,"preview switching does not accumulate handles");
    // Exercise actual Space key handling and painting after a hidden prefetch.
    p.ClosePreview();p.Action(30,png_ids.front());
    for(int i=0;i<4;++i)settle();
    Require(p.preview_cache.Find(p.selected)!=nullptr&&!p.preview.IsOpen(),"selection preloads while preview is hidden");
    Require(p.preview_cache.Bytes()<=clipboard::PreviewCache::Limit,"preview cache has a fixed byte budget");
    p.Key(VK_SPACE);Pump();
    Require(clipboard::PreviewWindowTest::HasImage(p.preview),"first Space uses prefetched pixels without loading");
    const auto space_window=clipboard::PreviewWindowTest::Window(p.preview);
    const auto space_renderer=clipboard::PreviewWindowTest::Renderer(p.preview);
    std::vector<double> space;
    for(int i=0;i<50;++i){
        p.Key(VK_SPACE);Require(!p.preview.IsOpen(),"Space hides preview");
        const auto start=Clock::now();p.Key(VK_SPACE);UpdateWindow(space_window);space.push_back(Ms(start));
        Require(!clipboard::PreviewWindowTest::Loading(p.preview)&&clipboard::PreviewWindowTest::HasImage(p.preview),"Space reopens without waiting for decode");
        Require(clipboard::PreviewWindowTest::Window(p.preview)==space_window&&clipboard::PreviewWindowTest::Renderer(p.preview)==space_renderer,"Space reuses native and drawing resources");
    }
    Times("space_reopen_through_paint",space);
    p.preview.Hide();Sample("hidden_preview_warm");
    const auto idle_start=Clock::now();
    while(Ms(idle_start)<11000){Pump();Sleep(20);}
    Require(clipboard::PreviewWindowTest::Released(p.preview),"real idle timer releases hidden preview resources");
    Require(!device_lifetime.expired(),"hidden preview release retains the live list device");
    p.Present();
    Sample("hidden_preview_expired");
    const auto reopen_start=Clock::now();p.Key(VK_SPACE);Pump();
    Require(clipboard::PreviewWindowTest::HasImage(p.preview),"idle reopen reuses cached decoded image");
    std::cout<<"IDLE_REOPEN ms="<<Ms(reopen_start)<<std::endl;
    p.Fold();settle();Require(p.preview_cache.Bytes()==0&&p.thumbnail_cache.Bytes()==0,"fold releases both pixel caches");
    p.ClosePreview();settle();Pump();Require(clipboard::PreviewWindowTest::Released(p.preview),"preview close releases resources");Sample("preview_closed");
    p.Fold();settle();p.Present();Pump();
    Require(p.images.empty()&&p.file_bitmaps.empty()&&p.file_icons->Size()==0&&clipboard::PreviewWindowTest::Released(p.preview),"fold clears content caches");
    const auto stats=p.disk->Stats();Require(stats.pending_bytes==0&&stats.result_bytes==0&&stats.jobs==0,"fold drains queues");
    std::cout<<"FOLDED history_entries="<<p.history.entries.size()<<" queue_jobs="<<stats.jobs<<" queued_payload_bytes="<<stats.pending_bytes<<" result_bytes="<<stats.result_bytes
        <<" thumbnail_entries="<<p.images.size()<<" icon_entries="<<p.file_icons->Size()<<" surface_bytes="<<static_cast<size_t>(p.width)*p.height*4<<std::endl;
    Sample("folded_painted");Idle("folded");
    Require(p.composition!=nullptr,"folded audit presents through the real desktop composition path");
    const auto folded_idle=Clock::now();
    while(Ms(folded_idle)<11000){Pump();Sleep(20);}
    Sample("folded_driver_trimmed");Require(!p.factory&&!p.target&&p.composition!=nullptr,"folded idle releases software renderer caches but keeps its presented strip");
    if(GetEnvironmentVariableW(L"LUMASHOT_PROFILE_HEAPS",nullptr,0)){
        HeapSnapshot("before_optimize");Sample("heap_before_optimize");
        HEAP_OPTIMIZE_RESOURCES_INFORMATION info{HEAP_OPTIMIZE_RESOURCES_CURRENT_VERSION,0};
        const auto start=Clock::now();const BOOL optimized=HeapSetInformation(nullptr,HeapOptimizeResources,&info,sizeof(info));
        std::cout<<"HEAP_OPTIMIZE success="<<optimized<<" ms="<<Ms(start)<<std::endl;
        Sample("heap_after_optimize");HeapSnapshot("after_optimize");
    }
    {   // Idle folded strip: layered copy replaces the D3D11/DComp device, repaints stay layered, input wakes it.
        ShowWindow(p.folded_window,SW_SHOWNOACTIVATE);p.Present();Pump();
        Require(p.composition!=nullptr&&!p.idle_layered,"visible folded strip presents through composition first");
        Sample("folded_visible");
        const auto idle_device=ClipboardCompositionTest::Lifetime(*p.composition);
        const auto strip_start=Clock::now();p.ReleaseRenderer();p.EnterIdleStrip();const double enter_ms=Ms(strip_start);Pump();
        Require(p.idle_layered&&!p.composition&&p.idle_strip&&IsWindowVisible(p.idle_strip),"idle folded strip swaps to a layered copy and releases composition");
        RECT folded_rect{},idle_rect{};GetWindowRect(p.folded_window,&folded_rect);GetWindowRect(p.idle_strip,&idle_rect);
        Require(EqualRect(&folded_rect,&idle_rect)!=FALSE,"layered copy covers the folded strip exactly");
        Require((GetWindowLongPtrW(p.idle_strip,GWL_EXSTYLE)&(WS_EX_TRANSPARENT|WS_EX_LAYERED|WS_EX_NOACTIVATE))==(WS_EX_TRANSPARENT|WS_EX_LAYERED|WS_EX_NOACTIVATE),"layered copy never takes input or focus");
        Require(idle_device.expired(),"idle folded strip releases the shared D3D11 device");
        Sample("folded_idle_layered");
        const auto repaint_start=Clock::now();p.Present();const double repaint_ms=Ms(repaint_start);Pump();
        Require(p.idle_layered&&!p.composition&&IsWindowVisible(p.idle_strip),"idle repaint (count change) stays on the layered copy");
        p.ReleaseRenderer();Pump();Sample("folded_idle_repainted");
        std::cout<<"IDLE_STRIP enter_ms="<<enter_ms<<" repaint_ms="<<repaint_ms<<std::endl;
    }
    const auto folded_reopen=Clock::now();open();
    Require(!p.idle_layered&&!IsWindowVisible(p.idle_strip)&&p.composition!=nullptr,"opening from idle restores composition and hides the layered copy");
    std::cout<<"FOLD_REOPEN ms="<<Ms(folded_reopen)<<std::endl;
    {   // Reopen latency from each folded state, 5 runs each. Pointer path: hovering the idle
        // strip creates the device on a worker before the click; keyboard path pays it inline.
        std::vector<double> reopen_warm,cold,hovered;
        for(int run=0;run<5;++run){
            p.Fold();settle();ShowWindow(p.folded_window,SW_SHOWNOACTIVATE);p.Present();Pump();Sleep(50);
            auto t=Clock::now();open();reopen_warm.push_back(Ms(t));
            p.Fold();settle();ShowWindow(p.folded_window,SW_SHOWNOACTIVATE);p.Present();Pump();p.ReleaseRenderer();p.EnterIdleStrip();Pump();Sleep(50);
            Require(p.idle_layered&&!p.composition&&!p.parked_composition,"strip re-enters idle");
            t=Clock::now();open();cold.push_back(Ms(t));
            p.Fold();settle();ShowWindow(p.folded_window,SW_SHOWNOACTIVATE);p.Present();Pump();p.ReleaseRenderer();p.EnterIdleStrip();Pump();
            SendMessageW(p.folded_window,WM_MOUSEMOVE,0,MAKELPARAM(4,4));
            Require(ClipboardComposition::Prewarmed(),"hovering the idle strip prewarms graphics off the UI thread");
            Sleep(150);Pump();t=Clock::now();open();hovered.push_back(Ms(t));
            Require(!ClipboardComposition::Prewarmed()&&!p.idle_layered&&p.composition!=nullptr,"reopen consumes the prewarmed device");
        }
        Times("reopen_from_warm_fold",reopen_warm);Times("reopen_from_idle_strip_keyboard",cold);Times("reopen_from_idle_strip_hover",hovered);
    }
    p.Fold();settle();p.Present();Pump();
    const auto session=p.disk->Directory();auto t=Clock::now();p.Enable(false,false);const auto first_disable=Ms(t);Pump();
    Require(!p.disk&&!p.window&&!p.search&&!p.factory&&!p.writer&&!p.surface&&!p.file_icons&&p.history.entries.empty()&&clipboard::PreviewWindowTest::Released(p.preview),"disable releases owned resources");
    Require(device_lifetime.expired(),"last window releases shared graphics device without a global strong cache");
    Require(!std::filesystem::exists(session),"disable deletes encrypted session");
    std::cout<<"DISABLE first_ms="<<first_disable<<std::endl;Sample("disabled100history");
    std::vector<double> disabling;std::vector<size_t> disabled_private;Memory disabled_warm{},disabled_last{};
    for(int i=1;i<=cycles;++i){
        Require(p.Enable(true,i%2!=0),"re-enable panel");p.disk=std::make_unique<clipboard::SessionStore>(root/L"store");settle();
        save(Dib(i));clipboard::Entry file;file.kind=clipboard::Kind::Files;file.text=L"synthetic.pdf";file.formats.push_back({CF_HDROP,{0,1,2,3}});save(std::move(file));
        open();Require(p.file_icons->WaitIdle(10000),"synthetic file icon finishes");p.Render();
        for(const auto& e:p.history.entries)if(e.kind==clipboard::Kind::Image)p.selected=e.id;
        p.ShowPreview();settle();{auto frame=clipboard::PreviewWindowTest::Snapshot(p.preview);Require(!frame.pixels.empty(),"decode before disable");}
        const auto directory=p.disk->Directory();t=Clock::now();p.Enable(false,false);disabling.push_back(Ms(t));Pump();
        Require(!p.disk&&!p.window&&!p.search&&!p.surface&&!p.factory&&!p.writer&&!p.file_icons&&p.images.empty()&&p.file_bitmaps.empty()&&p.history.entries.empty(),"disable clears workers and panel resources");
        Require(clipboard::PreviewWindowTest::Released(p.preview)&&!std::filesystem::exists(directory),"disable clears active preview and session directory");
        disabled_private.push_back(ReadMemory().bytes);
        if(i==10)disabled_warm=ReadMemory();if(i==1||i==10||i==50||i==100||i==cycles)disabled_last=Sample("disable_cycle",i);
    }
    Times("disable_ui_join_and_cleanup",disabling);
    // The process heap re-commits and lazily decommits free pages as each cycle's
    // transient allocations churn, so single samples swing by ~15-20 MB with live
    // heap unchanged. A leak raises every later sample; compare floors instead.
    const auto floor=[&](size_t first,size_t last){return *std::min_element(disabled_private.begin()+static_cast<std::ptrdiff_t>(first),disabled_private.begin()+static_cast<std::ptrdiff_t>(last));};
    const size_t half=disabled_private.size()/2,warm_first=half>=5?half-5:0,tail_first=disabled_private.size()>=5?disabled_private.size()-5:0;
    const size_t warm_floor=floor(warm_first,std::max(half,warm_first+1)),tail_floor=floor(tail_first,disabled_private.size());
    std::cout<<"DISABLE_FLOOR warm="<<warm_floor<<" tail="<<tail_floor<<std::endl;
    Require(tail_floor<=warm_floor+16*1024*1024,"disable cycles retain bounded private memory");
    Require(disabled_last.gdi<=disabled_warm.gdi+2&&disabled_last.user<=disabled_warm.user+2&&disabled_last.handles<=disabled_warm.handles+8,"disable cycles retain bounded handles");
    Idle("disabled");Sample("disabled_settled");
    std::cout<<"PASS resource audit cycles="<<cycles<<" (synthetic process, not real clipboard or target-app paste)"<<std::endl;return 0;
}
};
}
int main(int argc,char** argv){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int result=1;
    try{result=lumashot::ClipboardPanelTest::Run(argc>1?std::clamp(std::atoi(argv[1]),10,500):20);}
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;}
    CoUninitialize();return result;
}