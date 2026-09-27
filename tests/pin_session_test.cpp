#include "../src/pin/pin.cpp"
#include "app/startup.h"
#include "app/preferences.h"
#include <iostream>
#include <fstream>
namespace lumashot {
struct PinSessionLifecycleTest {
static int Child(const std::wstring& mode,const std::filesystem::path& path){
    PinManager manager(nullptr);if(!manager.EnableSession(path))return 2;
    if(mode==L"--write"||mode==L"--shutdown"){
        const auto frame=MakeFrame({0,0,90,65},0xff89abcd);manager.Create(frame,frame,{160,180},std::nullopt,{},false);manager.Create(frame,frame,{320,240},std::nullopt,{},false);
        if(mode==L"--shutdown"){for(auto& [id,pin]:manager.pins_){if(SendMessageW(pin->window,WM_QUERYENDSESSION,0,0)!=TRUE)return 3;SendMessageW(pin->window,WM_ENDSESSION,TRUE,0);DestroyWindow(pin->window);}}
        return 0;
    }
    if(mode==L"--close-one"){if(manager.pins_.size()!=2)return 4;DestroyWindow(manager.pins_.begin()->second->window);return manager.FlushSession()?0:5;}
    const size_t expected=mode==L"--read-two"?2:1;return manager.pins_.size()==expected?0:6;
}
static bool Process(const wchar_t* mode,const std::filesystem::path& path){
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);std::wstring command=L"\""+std::wstring(executable)+L"\" "+mode+L" \""+path.native()+L"\"";
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};if(!CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))return false;
    const DWORD waited=WaitForSingleObject(process.hProcess,20000);if(waited!=WAIT_OBJECT_0)TerminateProcess(process.hProcess,99);DWORD code=99;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hThread);CloseHandle(process.hProcess);return waited==WAIT_OBJECT_0&&code==0;
}

static int Run(){
    int failures=0;const auto expect=[&](bool ok,const char* name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<std::endl;failures+=!ok;};
    const auto root=std::filesystem::temp_directory_path()/(L"LumaShot-session-test-"+std::to_wstring(GetCurrentProcessId()));std::filesystem::create_directories(root);
    struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove_all(path,ec);}}cleanup{root};
    auto image=std::make_shared<Frame>(MakeFrame({0,0,120,80},0xffabcdee));for(size_t i=0;i<image->pixels.size();++i)if(i%7==0)image->pixels[i]=0xff112233;
    PinSessionRecord first;first.id=1;first.origin={-600.25f,180.5f};first.zoom=1.4f;first.locked=true;first.style=PinStyle::Curl;first.image=first.ocr_image=first.base=image;
    Mark mark;mark.tool=Tool::Number;mark.rotation=23;mark.corner_radii=std::array<float,4>{1,2,3,4};mark.number_detail=Box{11,12,34,45};mark.number_leader=std::array<Point,4>{Point{1,2},Point{3,4},Point{5,6},Point{7,8}};mark.a={2,3};mark.b={25,32};mark.color=0xff123456;mark.width=7;mark.pen_opacity=.42f;mark.pen_mode=PenMode::Highlighter;mark.pen_smoothing=PenSmoothing::High;mark.line_style=LineStyle::DashDot;mark.arrow_style=ArrowStyle::Double;mark.arrow_type=ArrowType::HandDrawn;mark.arrow_head=ArrowHead::Arc;mark.arrow_size=17;mark.font_size=33;mark.number=7;mark.number_label=L"这里 你好";mark.number_size=28;mark.number_shape=NumberShape::Capsule;mark.number_combo=NumberCombo::Leader;mark.number_text_color=0xff765432;mark.number_text_preset=3;mark.number_text_size=18;mark.number_target={98,60};mark.text_bold=true;mark.font_family=L"Microsoft YaHei UI";mark.text_auto_size=true;mark.text_wrap_width=114;mark.text_align=TextAlign::Right;mark.text_background=TextBackground::Automatic;mark.mosaic_cell=19;mark.mosaic_brush=41;mark.mosaic_mode=MosaicMode::Blur;mark.mosaic_method=MosaicMethod::Rectangle;mark.fill_color=0xffabcdef;mark.fill_opacity=.6f;mark.corner_radius=8;mark.text=L"第一行\r\n第二行";mark.points={{1,2},{3,7},{9,11}};
    first.annotations={mark};first.decorations={{TextDecoration::Wave,{{4,5,20,25}}}};
    auto second=first;second.id=2;second.locked=false;second.origin={600,380};second.zoom=.75f;second.image=std::make_shared<Frame>(MakeFrame({0,0,120,80},0xff334455));
    PinSessionStore store(root/L"store");store.Save({first,second});unsigned skipped{};auto loaded=store.Load(skipped);
    expect(loaded.size()==2&&!skipped,"two independent pins load from atomic manifest");expect(loaded[0].image->pixels==first.image->pixels&&loaded[1].image->pixels==second.image->pixels,"PNG content round trips exactly");expect(loaded[1].base->pixels==first.base->pixels&&loaded[1].ocr_image->pixels==first.ocr_image->pixels,"editable base and OCR source survive separately from display image");
    expect(loaded[0].annotations==first.annotations&&loaded[0].decorations[0].boxes==first.decorations[0].boxes,"every annotation field and text decoration survives serialization");expect(loaded[0].origin==first.origin&&loaded[0].zoom==first.zoom&&loaded[0].locked&&loaded[0].style==first.style,"position zoom lock and sticker style preserved");
    std::map<std::filesystem::path,std::filesystem::file_time_type> content;for(const auto& entry:std::filesystem::directory_iterator(root/L"store"))if(entry.path().extension()==L".dat")content[entry.path()]=entry.last_write_time();
    first.origin={250,160};store.Save({first,second});bool unchanged=true;for(const auto& [path,time]:content)unchanged&=std::filesystem::last_write_time(path)==time;expect(unchanged,"geometry-only save does not recompress or rewrite image payloads");
    // Close one pin: only its content is removed after the index is committed.
    store.Save({first});expect(store.Load(skipped).size()==1,"closed pin absent from next restore");size_t files=0;for(const auto& entry:std::filesystem::directory_iterator(root/L"store"))files+=entry.path().extension()==L".dat";expect(files==1,"closed screenshot payload reclaimed");
    store.Save({});expect(store.Load(skipped).empty()&&!skipped,"explicit empty session remains empty");store.Save({first});
    std::filesystem::path payload;for(const auto& entry:std::filesystem::directory_iterator(root/L"store"))if(entry.path().extension()==L".dat")payload=entry.path();{std::ofstream broken(payload,std::ios::binary|std::ios::trunc);broken<<"bad";}
    expect(store.Load(skipped).empty()&&skipped==1,"corrupt image payload skipped without crashing");store.Save({});expect(std::filesystem::exists(payload)&&store.Load(skipped).empty()&&skipped==1,"unreadable payload is retained for later recovery, not silently deleted");
    {std::ofstream broken(root/L"store"/L"session.index",std::ios::binary|std::ios::trunc);broken<<"bad";}bool rejected=false;try{store.Load(skipped);}catch(...){rejected=true;}expect(rejected,"malformed manifest rejected rather than interpreted as empty session");
    const auto lifecycle=root/L"lifecycle";
    {
        PinManager manager(nullptr);expect(manager.EnableSession(lifecycle),"enable empty isolated session");manager.Create(*image,*image,{220,180},std::nullopt,{},false);
        auto& pin=*manager.pins_.begin()->second;manager.ToggleLock(pin);pin.zoom=pin.zoom_goal=1.5f;manager.ApplyPaper(pin,{180,150});pin.annotations.marks={mark};++pin.revision;manager.SessionChanged();expect(manager.FlushSession(),"live pin checkpoint flushed on worker");
        manager.PreserveSession();DestroyWindow(pin.window);expect(manager.FlushSession(),"shutdown destruction does not remove preserved open pin");
    }
    // PreserveSession must not be overwritten with an empty snapshot after destruction.
    {
        PinManager manager(nullptr);expect(manager.EnableSession(lifecycle),"restore manager session in fresh instance");expect(manager.pins_.size()==1,"unclosed pin restored after manager restart");
        if(!manager.pins_.empty()){auto& pin=*manager.pins_.begin()->second;expect(pin.locked&&std::abs(pin.zoom-1.5f)<.001f&&pin.annotations.marks==std::vector<Mark>{mark},"restored live window keeps lock zoom and editable annotations");RECT bounds{};GetWindowRect(pin.window,&bounds);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(pin.window,MONITOR_DEFAULTTONEAREST),&monitor);expect(bounds.left>=monitor.rcWork.left&&bounds.top>=monitor.rcWork.top,"restored pin remains reachable on current monitor");DestroyWindow(pin.window);}
        expect(manager.FlushSession(),"explicit close durably removes restored pin");
    }
    {PinManager manager(nullptr);expect(manager.EnableSession(lifecycle)&&manager.pins_.empty(),"closed pin does not reappear on subsequent restart");}
    expect(Process(L"--write",root/L"process")&&Process(L"--read-two",root/L"process"),"separate process exits and restarts restore both unclosed pins");
    expect(Process(L"--close-one",root/L"process")&&Process(L"--read-one",root/L"process"),"explicit close survives another process restart");
    expect(Process(L"--shutdown",root/L"shutdown")&&Process(L"--read-two",root/L"shutdown"),"Windows end-session messages preserve pins while their windows are destroyed");
    Preferences prefs;expect(prefs.start_with_windows,"startup option defaults enabled");prefs.start_with_windows=false;prefs.SaveTo(root/L"settings.ini");expect(!Preferences::LoadFrom(root/L"settings.ini").start_with_windows,"startup opt-out persists");
    const std::wstring key=L"Software\\LumaShot\\Tests\\Startup-"+std::to_wstring(GetCurrentProcessId());const auto executable=std::filesystem::path(L"C:\\Synthetic Folder\\中文\\LumaShot.exe");
    expect(SetLoginStartup(true,executable,key.c_str()),"enable startup in isolated registry key");wchar_t value[512]{};DWORD bytes=sizeof(value);expect(RegGetValueW(HKEY_CURRENT_USER,key.c_str(),L"LumaShot",RRF_RT_REG_SZ,nullptr,value,&bytes)==ERROR_SUCCESS&&std::wstring(value)==LoginStartupCommand(executable),"startup command quotes path and uses background launch");
    expect(SetLoginStartup(false,executable,key.c_str())&&SetLoginStartup(false,executable,key.c_str()),"startup disable is idempotent");RegDeleteTreeW(HKEY_CURRENT_USER,key.c_str());
    return failures?1:0;
}
};
}
int wmain(int argc,wchar_t** argv){SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);const int result=argc==3?lumashot::PinSessionLifecycleTest::Child(argv[1],argv[2]):lumashot::PinSessionLifecycleTest::Run();CoUninitialize();return result;}
