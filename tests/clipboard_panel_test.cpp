// Includes the actual panel implementation to exercise hidden, synthetic UI.
// No clipboard listener, GetClipboardData, SetClipboardData or SendInput in this test.
#include "clipboard_preview_probe.h"
#include <fstream>
#include "../src/clipboard/panel.cpp"
#include "export/png.h"
#include <iostream>
#include <psapi.h>
namespace lumashot {
struct ClipboardPanelTest {
static LRESULT CALLBACK Backdrop(HWND w,UINT m,WPARAM wp,LPARAM lp){
    if(m==WM_PAINT){PAINTSTRUCT ps{};BeginPaint(w,&ps);RECT area{};GetClientRect(w,&area);FillRect(ps.hdc,&area,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));RECT stripe{150,0,200,area.bottom};FillRect(ps.hdc,&stripe,static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));EndPaint(w,&ps);return 0;}return DefWindowProcW(w,m,wp,lp);
}
static bool CaptureFixture(HWND panel,HWND background,const wchar_t* path,bool padded_liquid=false){
    RECT r{},back{};GetWindowRect(panel,&r);GetWindowRect(background,&back);
    if(r.left<back.left||r.top<back.top||r.right>back.right||r.bottom>back.bottom){std::cout<<"CAPTURE rectangle outside synthetic fixture"<<std::endl;return false;}
    // Capture only the area covered by our two synthetic windows.
    for(int y=r.top+2;y<r.bottom;y+=12)for(int x=r.left+2;x<r.right;x+=12){const auto top=WindowFromPoint({x,y});if(top!=panel&&top!=background&&!IsChild(panel,top)){std::cout<<"CAPTURE covered at "<<x<<","<<y<<std::endl;return false;}}
    DibSurface pixels(r.right-r.left,r.bottom-r.top);HDC screen=GetDC(nullptr);const bool ok=BitBlt(pixels.Dc(),0,0,r.right-r.left,r.bottom-r.top,screen,r.left,r.top,SRCCOPY|CAPTUREBLT)!=FALSE;ReleaseDC(nullptr,screen);
    if(!ok){std::cout<<"CAPTURE BitBlt error "<<GetLastError()<<std::endl;return false;}const bool clean=(pixels.Pixels()[0]&0xffffff)==0&&(pixels.Pixels()[r.right-r.left-1]&0xffffff)==0&&(pixels.Pixels()[size_t(r.bottom-r.top-1)*(r.right-r.left)]&0xffffff)==0&&(pixels.Pixels()[size_t(r.bottom-r.top)*(r.right-r.left)-1]&0xffffff)==0;std::cout<<"CORNER clean="<<clean<<std::endl;auto frame=MakeFrame({0,0,r.right-r.left,r.bottom-r.top});std::copy_n(pixels.Pixels(),frame.pixels.size(),frame.pixels.begin());for(auto& pixel:frame.pixels)pixel|=0xff000000;SavePng(frame,path);
    bool smooth=true;const int width=frame.Width(),height=frame.Height();
    for(int corner=0;corner<4;++corner){
        unsigned peak=0;std::vector<unsigned> levels;
        for(int y=0;y<12;++y)for(int x=0;x<12;++x){const auto value=frame.pixels[size_t(corner&2?height-1-y:y)*width+(corner&1?width-1-x:x)]&255;peak=std::max(peak,value);levels.push_back(value);}
        std::sort(levels.begin(),levels.end());levels.erase(std::unique(levels.begin(),levels.end()),levels.end());
        const auto partial=std::count_if(levels.begin(),levels.end(),[&](unsigned value){return value>0&&value+8<peak;});smooth&=partial>=4;
    }
    HRGN region=CreateRectRgn(0,0,0,0);const int region_kind=GetWindowRgn(panel,region);const bool no_region=region_kind==ERROR;DeleteObject(region);
    std::cout<<"CORNER antialias="<<smooth<<" no_region="<<no_region<<std::endl;return clean&&(padded_liquid||smooth)&&(padded_liquid?(region_kind==SIMPLEREGION||region_kind==COMPLEXREGION):no_region); // Liquid-edge AA and conservative input masks have dedicated native-renderer tests.
}
static int Appearance(){
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int failures=0;auto expect=[&](bool ok,const char* message){std::cout<<(ok?"PASS ":"FAIL ")<<message<<std::endl;failures+=!ok;};
    const auto root=std::filesystem::temp_directory_path()/(L"LumaShot-ui-test-"+std::to_wstring(GetCurrentProcessId()));
    struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code ec;std::filesystem::remove_all(p,ec);}}cleanup{root};
    ClipboardPanel panel(nullptr,[]{});auto& p=*panel.impl_;p.test_mode=true;expect(p.Enable(true,false),"create acrylic clipboard fixture");p.disk=std::make_unique<clipboard::SessionStore>(root);expect(p.disk->WaitIdle(),"initialize isolated file-list storage");
    clipboard::Entry files;files.kind=clipboard::Kind::Files;GetLocalTime(&files.time);
    for(int i=0;i<12;++i){if(i)files.text+=L'\n';files.text+=L"C:\\Synthetic\\"+std::wstring(150,L'x')+L"\\"+(i%3==0?L"项目说明":i%3==1?L"预算清单":L"参考图片")+std::to_wstring(i)+(i%3==0?L".docx":i%3==1?L".xlsx":L".png");}
    files.formats.push_back({CF_HDROP,{1,2,3}});expect(p.disk->Save(files)&&p.disk->WaitIdle(),"store long multifile fixture");p.DrainStore();
    expect(p.history.entries.front().file_count==12&&p.history.entries.front().text.size()<=513&&!p.history.entries.front().file_names[2].empty(),"exact file count and three samples survive summary truncation");
    const auto id=p.history.entries.front().id;p.expanded=true;p.Filter();p.selected=id;
    expect(p.disk->LoadPreview(p.history.entries.front(),777)&&p.disk->WaitIdle(),"load complete file list off UI thread");{auto results=p.disk->Take();expect(results.size()==1&&results[0].preview.file_count==12&&results[0].preview.text.find(L"11.png")!=std::wstring::npos,"preview contains filenames beyond shortened metadata");}
    for(const auto& value:{L"https://example.com/design",L"让信息清楚呈现，让每次复制都更顺手。",L"const message = 'Hello, LumaShot';"}){clipboard::Entry entry;entry.kind=clipboard::Kind::Text;entry.text=value;GetLocalTime(&entry.time);const auto* bytes=reinterpret_cast<const unsigned char*>(entry.text.c_str());entry.formats.push_back({CF_UNICODETEXT,{bytes,bytes+(entry.text.size()+1)*2}});p.history.Add(std::move(entry));}
    p.history.Find(id)->favorite=true;p.Filter();p.selected=id;
    for(bool dark:{false,true})for(float scale:{1.f,1.5f,2.f}){
        p.Enable(true,dark);p.expanded=true;p.scale=scale;p.Filter();SetWindowPos(p.window,nullptr,0,0,int(p.W*scale),int(p.H*scale),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);p.LayoutSearch();p.Render();expect(p.file_icons->WaitIdle(10000),"all sample file icons finish");p.Render();
        const auto pixel=[&](float x,float y){return p.surface->Pixels()[size_t(y*scale)*p.width+size_t(x*scale)];};
        expect(p.width==int(400*scale),"compact width follows DPI");expect(pixel(18,p.ListTop+30)==0xff689af0&&p.app_icon,"selected card has accent rail and compiled SVG vector branding");
        expect(p.custom_outline?p.surface->Pixels()[0]==p.surface->Pixels()[size_t(690*scale)*p.width+size_t(200*scale)]:p.surface->Pixels()[0]==0,"one outline owner: compositor clips acrylic, opaque fallback clips pixels");
        const auto alpha=pixel(200,690)>>24;expect(p.acrylic?(alpha>0&&alpha<255):alpha==255,"expanded backdrop retains translucency with opaque fallback");
        expect((GetWindowLongPtrW(p.window,GWL_EXSTYLE)&WS_EX_LAYERED)==0,"normal HWND allows system antialiased corners");
        auto frame=MakeFrame({0,0,p.width,p.height});for(size_t i=0;i<frame.pixels.size();++i){const auto src=p.surface->Pixels()[i],a=src>>24;const uint32_t base=dark?0x263142:0xf0f5fc;uint32_t out=0xff000000;for(int shift:{0,8,16})out|=std::min(255u,((src>>shift)&255)+((base>>shift)&255)*(255-a)/255)<<shift;frame.pixels[i]=out;}
        const auto name=L"build/clipboard-compact-"+std::wstring(dark?L"dark-":L"light-")+std::to_wstring(int(scale*100))+L".png";SavePng(frame,name);
        p.expanded=false;SetWindowPos(p.window,nullptr,0,0,int(p.CW*scale),int(p.CH*scale),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);p.Render();const auto folded=p.surface->Pixels()[size_t(65*scale)*p.width+size_t(13*scale)]>>24;expect(folded==255,"folded fallback uses stable opaque material at every DPI");
    }
    expect(p.custom_outline,"system accepts unified rounded outline without an extra border");
    WNDCLASSW backdrop{};backdrop.hInstance=GetModuleHandleW(nullptr);backdrop.lpfnWndProc=Backdrop;backdrop.lpszClassName=L"LumaShot.SyntheticAcrylicBackdrop";RegisterClassW(&backdrop);
    // This checks free-floating DWM corners, not edge adhesion. Keep the whole
    // fixture beyond the tether range; never relax CaptureFixture's privacy guard.
    MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(p.window,MONITOR_DEFAULTTONEAREST),&monitor);
    const int inset=static_cast<int>(clipboard::LiquidDetachDistance)+20;
    const int x=monitor.rcWork.left+inset,y=monitor.rcWork.top+inset;
    HWND background=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,backdrop.lpszClassName,L"Synthetic checkerboard",WS_POPUP,x,y,900,784,nullptr,nullptr,backdrop.hInstance,nullptr);
    ShowWindow(background,SW_SHOWNOACTIVATE);UpdateWindow(background);p.pinned=true;
    {   // Regression: a never-shown Acrylic HWND must not leave a DWM backdrop on screen.
        const HWND parked=p.native_window;expect(parked&&parked!=p.window&&!IsWindowVisible(parked),"native panel stays hidden while folded");
        SetWindowPos(parked,HWND_TOPMOST,x+300,y+300,36,132,SWP_NOACTIVATE);DwmFlush();Sleep(150);
        const POINT probes[]={{x+318,y+366},{x+318,y+420}};bool covered=false,clean=true;HDC screen=GetDC(nullptr);
        for(const auto& point:probes){if(WindowFromPoint(point)!=background){covered=true;continue;}const COLORREF color=GetPixel(screen,point.x,point.y);std::cout<<"HIDDEN probe "<<point.x<<","<<point.y<<" = "<<std::hex<<color<<std::dec<<std::endl;if(color!=RGB(0,0,0))clean=false;}
        ReleaseDC(nullptr,screen);expect(!covered,"hidden panel probe area belongs to the synthetic fixture");
        expect(clean,"hidden acrylic panel leaves no ghost backdrop over synthetic background");
        SetWindowPos(parked,HWND_TOPMOST,0,0,36,132,SWP_NOACTIVATE);
    }
    for(bool dark:{false,true}){
        p.Enable(true,dark);p.expanded=true;p.scale=1;SetWindowPos(p.window,HWND_TOPMOST,x+20,y+20,400,744,SWP_NOACTIVATE);p.LayoutSearch();p.Present();ShowWindow(p.window,SW_SHOWNOACTIVATE);
        std::cout<<"ACRYLIC enabled="<<p.acrylic<<" dark="<<dark<<std::endl;
        DwmFlush();Sleep(80);expect(CaptureFixture(p.window,background,dark?L"build/clipboard-acrylic-live-dark.png":L"build/clipboard-acrylic-live-light.png"),"capture actual DWM backdrop over synthetic black background with clean exterior corners");
        p.expanded=false;SetWindowPos(p.window,HWND_TOPMOST,x+20,y+20,28,96,SWP_NOACTIVATE);p.Present();const auto folded_device=p.composition.get();SendMessageW(p.window,WM_TIMER,4,0);expect(p.composition.get()==folded_device,"idle trim preserves the displayed composition device");DwmFlush();Sleep(80);
        expect(CaptureFixture(p.window,background,dark?L"build/clipboard-acrylic-folded-dark.png":L"build/clipboard-acrylic-folded-light.png",true),"capture folded acrylic over synthetic background");
    }
    SetWindowPos(p.window,HWND_TOPMOST,x+740,y+580,28,96,SWP_NOACTIVATE);p.Present(); // Keep the preview corner fixture clear of the tab shadow.
    {clipboard::Entry text;text.id=987;text.kind=clipboard::Kind::Text;text.text=L"const message = 'Hello, LumaShot';\n\n文本与代码支持选择字符，正文直接显示在亚克力背景上。";
    for(bool night:{false,true}){p.preview.Show(p.window,text,{x+20,y+20,x+420,y+764},night,1,clipboard::PreparePreview(text));const HWND preview=clipboard::PreviewWindowTest::Window(p.preview);SetWindowPos(preview,HWND_TOPMOST,x+20,y+20,540,460,SWP_NOACTIVATE);UpdateWindow(preview);DwmFlush();Sleep(80);expect(CaptureFixture(preview,background,night?L"build/preview-minimal-dark.png":L"build/preview-minimal-light.png"),"preview acrylic has smooth system corners and no region");}p.preview.Close();}
    p.expanded=false;p.scale=1;SetWindowPos(p.window,HWND_TOPMOST,x+392,y+20,28,96,SWP_NOACTIVATE);p.Present();
    const RECT open_rect{x+20,y+20,x+420,y+764},fold_rect{x+392,y+20,x+420,y+116};
    const HWND liquid_hwnd=p.window,native_hwnd=p.native_window;
    p.expanded=true;expect(p.PresentAt(open_rect),"reveal independent acrylic panel");
    expect(p.window==native_hwnd&&p.window!=liquid_hwnd&&!p.liquid_drag_active,"expanded window does not morph or switch material");
    DwmFlush();Sleep(80);expect(CaptureFixture(p.window,background,L"build/clipboard-split-expanded.png"),"independent panel has native rounded corners");
    p.expanded=false;expect(p.PresentAt(fold_rect),"return to liquid tab");DwmFlush();
    expect(p.window==liquid_hwnd&&CaptureFixture(p.window,background,L"build/clipboard-split-folded.png",true),"folded tab keeps its own liquid region");
    p.expanded=true;p.PresentAt(open_rect);
    RECT finished{};GetWindowRect(p.window,&finished);expect(!p.liquid_drag_active&&EqualRect(&finished,&open_rect)&&p.window==native_hwnd,"reopen reuses exact native panel bounds");
    p.expanded=false;p.PresentAt(fold_rect);finished=p.VisualBodyBounds();
    expect(!p.liquid_drag_active&&EqualRect(&finished,&fold_rect)&&p.width==28&&p.height==96,"fold renders crisp final compact content without a morph surface");
    for(bool night:{false,true}){
        p.Enable(true,night);p.scale=1;p.expanded=false;p.PresentAt(fold_rect);p.expanded=true;
        p.PresentAt(open_rect);ShowWindow(p.window,SW_HIDE);
        const auto now=GetTickCount64();expect(p.StartOpening(fold_rect,open_rect,now),"pill opening starts with cached synthetic content");
        for(const auto elapsed:{0,60,120,220,349}){
            p.OpeningTick(now+elapsed);DwmFlush();
            const auto name=L"build/clipboard-opening-"+std::wstring(night?L"dark-":L"light-")+std::to_wstring(elapsed)+L".png";
            expect(p.opening&&!IsWindowVisible(native_hwnd),"native acrylic stays hidden throughout morph");
            expect(CaptureFixture(liquid_hwnd,background,name.c_str(),true),"capture opening over synthetic background only");
        }
        p.OpeningTick(now+350);expect(!p.opening&&!IsWindowVisible(liquid_hwnd)&&IsWindowVisible(native_hwnd),"opening hands off to native acrylic and stops timer");
        DwmFlush();Sleep(80);
        expect(CaptureFixture(native_hwnd,background,night?L"build/clipboard-opening-dark-350.png":L"build/clipboard-opening-light-350.png"),"capture native corners immediately after animation handoff");
        const auto prefix=L"build/clipboard-opening-"+std::wstring(night?L"dark-":L"light-");
        const auto before=ReadPng(prefix+L"349.png"),after=ReadPng(prefix+L"350.png");
        int max_edge_delta=0;
        const auto edge=[&](const Frame& image,int offset_x,int offset_y,int corner,int row){
            const auto brightness=[&](int x,int y){
                if(corner&1)x=open_rect.right-open_rect.left-1-x;
                if(corner&2)y=open_rect.bottom-open_rect.top-1-y;
                const auto pixel=image.pixels[size_t(y+offset_y)*image.Width()+size_t(x+offset_x)];
                return float((pixel&255)+((pixel>>8)&255)+((pixel>>16)&255));
            };
            const float threshold=brightness(24,16)*.5f;
            int x=0;while(x<24&&brightness(x,row)<threshold)++x;return x;
        };
        for(int corner=0;corner<4;++corner)for(int row=1;row<16;++row)
            max_edge_delta=std::max(max_edge_delta,std::abs(edge(before,open_rect.left-p.opening_viewport.left,open_rect.top-p.opening_viewport.top,corner,row)-edge(after,0,0,corner,row)));
        std::cout<<"HANDOFF corner_edge_delta_px="<<max_edge_delta<<std::endl;
        expect(max_edge_delta<=2,"all four last-frame corner contours match native DWM within antialias tolerance");
        p.expanded=false;p.PresentAt(fold_rect);p.expanded=true;p.PresentAt(open_rect);ShowWindow(p.window,SW_HIDE);
        expect(p.StartOpening(fold_rect,open_rect,now+400),"restart opening");p.Fold();
        p.OpeningTick(now+800);expect(!p.opening&&!p.expanded&&p.window==liquid_hwnd&&!IsWindowVisible(native_hwnd),"fold cancels opening without late native reveal");
    }
    p.expanded=false;p.PresentAt(fold_rect);ShowWindow(p.window,SW_SHOWNOACTIVATE);p.anchor={fold_rect.right,fold_rect.top};p.placed=true;p.Show();
    expect(p.opening==p.MotionEnabled(),"Show uses morph when system client animations are enabled");
    if(p.opening){SendMessageW(p.folded_window,WM_KEYDOWN,VK_DOWN,1);expect(!p.opening&&p.expanded&&IsWindowVisible(native_hwnd),"keyboard settles opening immediately into interactive panel");}
    p.Fold();
    DestroyWindow(background);
    p.expanded=true;p.Place();p.Present();ShowWindow(p.window,SW_SHOWNOACTIVATE);SetFocus(p.search);SetWindowTextW(p.search,L"测试 search");p.Present();expect(p.search_text_glyphs>0&&p.caret_on,"search text rasterizes through LumaText with native caret state");
    SendMessageW(p.search,EM_SETSEL,0,2);p.Present();DWORD first{},last{};SendMessageW(p.search,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
    expect(first==0&&last==2,"native text selection survives acrylic composition");
    {const auto before=p.search_text_glyphs;p.ime_active=true;p.ime_original=L"prefix suffix";p.ime_first=7;p.ime_last=7;p.ime_cursor=2;p.ime_text=L"中文输入";p.Present();expect(p.search_text_glyphs>before,"IME composition is rendered through LumaText");p.ime_active=false;p.ime_text.clear();}
    SetFocus(p.search);SetWindowTextW(p.search,std::wstring(100,L'测').c_str());SendMessageW(p.search,EM_SETSEL,100,100);SendMessageW(p.search,EM_SCROLLCARET,0,0);SendMessageW(p.search,WM_CHAR,L'x',1);p.Present();expect(LOWORD(SendMessageW(p.search,EM_CHARFROMPOS,0,0))>0,"native horizontal scrolling survives LumaText rendering");
    p.OpenMore();p.Present();p.CloseMore();p.Fold();expect(p.search_text_glyphs==0,"fold resets search rendering state");p.Enable(false,false);return failures?1:0;
}
static int Features(){
    int failures=0;auto expect=[&](bool ok,const char* message){std::cout<<(ok?"PASS ":"FAIL ")<<message<<std::endl;failures+=!ok;};
    ClipboardPanel panel(nullptr,[]{});auto& p=*panel.impl_;p.test_mode=true;expect(p.Enable(true,false),"enable isolated feature audit");
    auto text=[](const std::wstring& value){clipboard::Entry e;e.text=value;e.kind=clipboard::Kind::Text;const auto* b=reinterpret_cast<const unsigned char*>(value.c_str());e.formats.push_back({CF_UNICODETEXT,{b,b+(value.size()+1)*2}});return e;};
    for(int i=0;i<12;++i)p.history.Add(text(i%3==0?L"https://example.com/long-wrapped-link-for-clipboard-layout/"+std::to_wstring(i):L"合成剪贴内容 "+std::to_wstring(i)));
    p.expanded=true;p.Filter();p.Place();p.Render();p.Key(VK_END);const auto old=p.selected;expect(p.scroll==7,"End reveals oldest item");
    const auto click=[&](int action,uint64_t id){p.Render();for(const auto& hit:p.hits)if(hit.action==action&&hit.id==id){const int x=static_cast<int>((hit.rect.left+hit.rect.right)*p.scale/2),y=static_cast<int>((hit.rect.top+hit.rect.bottom)*p.scale/2);SendMessageW(p.window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessageW(p.window,WM_LBUTTONUP,0,MAKELPARAM(x,y));return;}expect(false,"action has a rendered hit target");};
    click(31,old);expect(p.history.Find(old)->favorite&&p.visible.front()==old&&p.selected==old&&p.scroll==0,"actual row pin click moves entry to visible top and preserves selection");
    p.history.Add(text(L"新复制的合成内容"));p.Filter();expect(p.visible.front()==old,"new content cannot push pin down");
    p.Action(13,0);expect(p.visible.front()==old,"reverse sorting keeps pin at top");p.Action(13,0);
    p.Action(24,0);expect(p.visible.size()==1&&p.visible.front()==old,"pinned category shows actual pinned entries");p.Action(20,0);
    BYTE original_keys[256]{},favorite_keys[256]{};GetKeyboardState(original_keys);std::copy_n(original_keys,256,favorite_keys);favorite_keys[VK_CONTROL]=0x80;SetKeyboardState(favorite_keys);
    SendMessageW(p.window,WM_KEYDOWN,'D',1);expect(!p.history.Find(old)->favorite,"Ctrl+D removes selected favorite");
    SendMessageW(p.window,WM_KEYDOWN,'D',1LL|(1LL<<30));expect(!p.history.Find(old)->favorite,"held Ctrl+D does not toggle repeatedly");
    SendMessageW(p.window,WM_KEYDOWN,'D',1);expect(p.history.Find(old)->favorite,"Ctrl+D adds selected favorite");
    const auto query_before=p.query;SendMessageW(p.search,WM_KEYDOWN,'D',1);SendMessageW(p.search,WM_CHAR,4,1);
    expect(!p.history.Find(old)->favorite&&p.query==query_before,"search Ctrl+D toggles favorite without entering control text");
    SendMessageW(p.search,WM_KEYDOWN,'D',1);SetKeyboardState(original_keys);p.status.clear();
    for(bool dark:{false,true}){p.dark=dark;p.scale=dark?1.5f:1.f;SetWindowPos(p.window,nullptr,0,0,static_cast<int>(p.W*p.scale),static_cast<int>(p.H*p.scale),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);p.Render();auto frame=MakeFrame({0,0,p.width,p.height});std::copy_n(p.surface->Pixels(),frame.pixels.size(),frame.pixels.begin());for(auto& pixel:frame.pixels){const UINT inverse=255-(pixel>>24);const UINT background=dark?0x151b23:0xf5f8fc;UINT out=0xff000000;for(int shift:{0,8,16})out|=std::min(255u,((pixel>>shift)&255)+((((background>>shift)&255)*inverse+127)/255))<<shift;pixel=out;}SavePng(frame,dark?L"build/clipboard-items-pinned-dark.png":L"build/clipboard-items-pinned-light.png");}
    click(31,old);expect(!p.history.Find(old)->favorite&&p.visible.back()==old&&p.selected==old&&p.scroll==8,"unpin restores age order and keeps acted-on row visible");
    p.Key(VK_HOME);expect(p.selected==p.visible.front()&&p.scroll==0,"Home reaches first row");p.Key(VK_NEXT);expect(p.selected==p.visible[5],"PageDown advances five rows");p.Key(VK_PRIOR);expect(p.selected==p.visible.front(),"PageUp returns five rows");
    SendMessageW(p.window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(static_cast<int>((p.W-8)*p.scale),static_cast<int>((p.ListTop+8)*p.scale)));
    SendMessageW(p.window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(static_cast<int>((p.W-8)*p.scale),static_cast<int>(682*p.scale)));
    SendMessageW(p.window,WM_LBUTTONUP,0,MAKELPARAM(static_cast<int>((p.W-8)*p.scale),static_cast<int>(682*p.scale)));
    expect(p.scroll==8&&!p.scroll_drag&&GetCapture()!=p.window,"native scrollbar drag reaches final row and releases capture");
    p.Key(VK_HOME);p.Key(VK_DOWN);const auto deleting=p.selected,next=p.visible[2];p.Action(34,deleting);expect(!p.history.Find(deleting)&&p.selected==next,"delete selects adjacent surviving row");
    p.selected=p.visible.front();p.scroll=4;const auto viewport_top=p.visible[4];const auto offscreen_selected=p.selected;
    click(34,p.visible[6]);expect(p.scroll==4&&p.visible[4]==viewport_top&&p.selected==offscreen_selected,"delete visible unselected row preserves scrolled viewport instead of revealing top selection");
    p.Action(34,offscreen_selected);expect(p.scroll==3&&p.visible[3]==viewport_top,"delete above viewport preserves first visible record");
    p.scroll=static_cast<int>(p.visible.size())-5;p.selected=p.visible.back();const int old_end=p.scroll;const auto last=p.selected;
    click(34,last);expect(!p.history.Find(last)&&p.scroll==old_end-1&&p.selected==p.visible.back(),"delete last row only backs up enough to fill viewport");
    while(!p.visible.empty())p.Action(34,p.visible.back());expect(p.scroll==0&&p.selected==0,"deleting final record leaves valid empty viewport");
    p.paste_target=nullptr;p.paste_inflight=true;p.PasteTick();expect(p.status.find(L"自动粘贴已取消")!=std::wstring::npos,"invalid paste destination cancels before sending input");
    p.CancelReads();p.history.Clear(false);
    const auto root=std::filesystem::temp_directory_path()/(L"LumaShot-feature-test-"+std::to_wstring(GetCurrentProcessId()));
    struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code ec;std::filesystem::remove_all(p,ec);}}cleanup{root};
    p.disk=std::make_unique<clipboard::SessionStore>(root);expect(p.disk->WaitIdle(),"initialize encrypted test history");
    for(int i=0;i<3;++i){expect(p.disk->Save(text(std::wstring(1600,L'测')+L" tail-match "+std::to_wstring(i))),"save long search fixture");expect(p.disk->WaitIdle(),"finish fixture save");p.DrainStore();}
    SetWindowTextW(p.search,L"tail-match");expect(p.disk->WaitIdle(),"full text search finishes");p.DrainStore();expect(p.visible.size()==3,"search finds text beyond summary");
    const auto match=p.visible.back();p.Action(31,match);expect(p.visible.size()==3&&p.visible.front()==match&&p.selected==match,"pinning long search result preserves matches and selection before async completion");
    expect(p.disk->WaitIdle(),"search after reorder finishes");p.DrainStore();expect(p.visible.front()==match,"async search preserves pinned ordering");
    const auto corrupt=p.history.entries.front();{std::ofstream file(p.disk->Directory()/(std::to_wstring(corrupt.payload->key)+L".bin"),std::ios::binary|std::ios::trunc);file<<"synthetic corrupted data";}
    p.Filter();expect(p.disk->WaitIdle(),"search with damaged entry completes");p.DrainStore();expect(p.visible.size()==2&&p.status.find(L"部分历史读取失败")!=std::wstring::npos,"one damaged payload does not hide other search matches");
    SetWindowTextW(p.search,L"");p.OpenMore();p.ChooseMore(1);expect(p.history.entries.size()==1&&p.history.Find(match),"clear unpinned preserves pinned payload");p.OpenMore();p.ChooseMore(2);p.ChooseMore(0);expect(p.history.Find(match)!=nullptr,"cancel full clear preserves pin");p.OpenMore();p.ChooseMore(2);p.ChooseMore(1);expect(p.history.entries.empty(),"confirmed full clear removes pinned items");
    p.Enable(false,false);return failures?1:0;
}
static int Groups(){
    int failures=0;auto expect=[&](bool ok,const char* message){std::cout<<(ok?"PASS ":"FAIL ")<<message<<std::endl;failures+=!ok;};
    ClipboardPanel panel(nullptr,[]{});auto& p=*panel.impl_;p.test_mode=true;expect(p.Enable(true,false),"enable isolated group audit");
    auto text=[](const std::wstring& value){clipboard::Entry e;e.text=value;e.kind=clipboard::Kind::Text;const auto* b=reinterpret_cast<const unsigned char*>(value.c_str());e.formats.push_back({CF_UNICODETEXT,{b,b+(value.size()+1)*2}});return e;};
    for(int i=0;i<6;++i)p.history.Add(text(L"合成分组内容 "+std::to_wstring(i)));
    p.expanded=true;p.Filter();p.Place();p.Render();
    auto center=[&](int action,uint64_t id,LPARAM& out){p.Render();for(const auto& hit:p.hits)if(hit.action==action&&hit.id==id){out=MAKELPARAM(static_cast<int>((hit.rect.left+hit.rect.right)*p.scale/2),static_cast<int>((hit.rect.top+hit.rect.bottom)*p.scale/2));return true;}return false;};
    auto click=[&](int action,uint64_t id){LPARAM at{};if(!center(action,id,at)){expect(false,"action has a rendered hit target");return;}SendMessageW(p.window,WM_LBUTTONDOWN,MK_LBUTTON,at);SendMessageW(p.window,WM_LBUTTONUP,0,at);};
    auto right_click=[&](int action,uint64_t id){LPARAM at{};if(!center(action,id,at)){expect(false,"right-click target is rendered");return;}SendMessageW(p.window,WM_RBUTTONDOWN,MK_RBUTTON,at);SendMessageW(p.window,WM_RBUTTONUP,0,at);};
    auto popup_index=[&](int command,uint32_t arg){for(size_t i=0;i<p.popup_items.size();++i)if(p.popup_items[i].command==command&&p.popup_items[i].arg==arg)return static_cast<int>(i);return -1;};
    auto click_popup=[&](int index){if(index<0||index>=static_cast<int>(p.popup_rows.size())){expect(false,"popup row exists");return;}const auto r=p.popup_rows[static_cast<size_t>(index)];const LPARAM at=MAKELPARAM(static_cast<int>((r.left+r.right)*p.scale/2),static_cast<int>((r.top+r.bottom)*p.scale/2));SendMessageW(p.window,WM_LBUTTONDOWN,MK_LBUTTON,at);SendMessageW(p.window,WM_LBUTTONUP,0,at);};
    auto search_text=[&]{wchar_t value[64]{};GetWindowTextW(p.search,value,64);return std::wstring(value);};
    BYTE original_keys[256]{},keys[256]{};GetKeyboardState(original_keys);
    auto hold=[&](bool ctrl,bool shift){std::copy_n(original_keys,256,keys);keys[VK_CONTROL]=ctrl?0x80:0;keys[VK_SHIFT]=shift?0x80:0;SetKeyboardState(keys);};
    auto release=[&]{SetKeyboardState(original_keys);};
    // "+" starts inline naming in the self-painted search box; Enter creates and opens the group.
    click(41,0);expect(p.naming==1&&search_text().empty(),"+ starts inline group naming");
    SetWindowTextW(p.search,L"工作");expect(p.query.empty()&&p.visible.size()==6,"typing a group name does not filter history");
    SendMessageW(p.search,WM_KEYDOWN,VK_RETURN,1);
    expect(!p.naming&&p.history.groups.size()==1&&p.history.groups[0].name==L"工作"&&p.tab==clipboard::History::GroupTabBase,"Enter creates group and opens its tab");
    expect(p.visible.empty()&&p.status.find(L"Ctrl+1")!=std::wstring::npos,"new empty group tab explains how to move items");
    const auto work=p.history.groups[0].id;LPARAM tab_hit{};expect(center(40,work,tab_hit),"group tab is rendered in the tab strip");
    p.Action(20,0);const auto first=p.visible[0],second=p.visible[1],third=p.visible[2];p.selected=first;
    // Ctrl+1..9 / Ctrl+0.
    hold(true,false);
    SendMessageW(p.window,WM_KEYDOWN,'1',1);expect(p.history.Find(first)->group==work&&p.status.find(L"已移入")!=std::wstring::npos,"Ctrl+1 moves selection into group 1");
    SendMessageW(p.window,WM_KEYDOWN,'1',1LL|(1LL<<30));expect(p.history.Find(first)->group==work,"held Ctrl+1 does not toggle repeatedly");
    SendMessageW(p.window,WM_KEYDOWN,'1',1);expect(p.history.Find(first)->group==0,"Ctrl+1 on the same group removes it");
    SendMessageW(p.window,WM_KEYDOWN,'1',1);SendMessageW(p.window,WM_KEYDOWN,'0',1);expect(p.history.Find(first)->group==0,"Ctrl+0 removes from group");
    SendMessageW(p.window,WM_KEYDOWN,'2',1);expect(p.status.find(L"没有第 2 个分组")!=std::wstring::npos,"missing group number reports status");
    SendMessageW(p.search,WM_KEYDOWN,'1',1);expect(p.history.Find(first)->group==work,"Ctrl+1 also works while search box has focus");
    SendMessageW(p.search,WM_KEYDOWN,'0',1);
    // Ctrl+Shift+N creates a group and moves the selection into it.
    hold(true,true);SendMessageW(p.window,WM_KEYDOWN,'N',1);release();
    expect(p.naming==1&&p.naming_entry==first,"Ctrl+Shift+N starts naming for the selected entry");
    SetWindowTextW(p.search,L"代码");SendMessageW(p.search,WM_KEYDOWN,VK_RETURN,1);
    expect(p.history.groups.size()==2&&p.history.Find(first)->group==p.history.groups[1].id&&p.tab==0,"new group receives the selection and the tab stays");
    const auto code=p.history.groups[1].id;
    // Duplicate names are rejected in place; Esc restores the previous search query.
    SetWindowTextW(p.search,L"合成");expect(p.query==L"合成","search query set before naming");
    p.BeginNaming(1,0,0);SetWindowTextW(p.search,L"工作");SendMessageW(p.search,WM_KEYDOWN,VK_RETURN,1);
    expect(p.naming==1&&p.status.find(L"同名")!=std::wstring::npos,"duplicate group name is rejected without leaving naming");
    SendMessageW(p.search,WM_KEYDOWN,VK_ESCAPE,1);expect(!p.naming&&p.query==L"合成"&&search_text()==L"合成","Esc cancels naming and restores search query");
    SetWindowTextW(p.search,L"");
    // Card folder button and right-click menu.
    click(35,second);expect(p.popup_open&&p.popup_entry==second&&GetCapture()==p.window,"card folder button opens group list");
    click_popup(popup_index(1,work));expect(!p.popup_open&&p.history.Find(second)->group==work,"choosing a group row moves the entry");
    right_click(30,second);expect(p.popup_open&&p.popup_items[static_cast<size_t>(std::max(0,popup_index(1,work)))].checked&&p.popup_items[static_cast<size_t>(std::max(0,popup_index(1,0)))].enabled,"right-click card shows current group and enabled remove");
    p.Key(VK_ESCAPE);expect(!p.popup_open&&GetCapture()!=p.window,"Esc closes group popup and releases capture");
    p.selected=third;hold(true,false);SendMessageW(p.window,WM_KEYDOWN,'G',1);release();
    expect(p.popup_open&&p.popup_hover==popup_index(1,work),"Ctrl+G opens keyboard-navigable group list");
    p.Key(VK_RETURN);expect(!p.popup_open&&p.history.Find(third)->group==work,"Enter chooses highlighted group");
    // Chips on cards in 全部; hidden inside group tabs.
    p.Render();{bool chip_hit=false;for(const auto& hit:p.hits)chip_hit|=hit.action==35&&hit.id==second;expect(chip_hit,"grouped cards keep folder button");}
    // Alt+←/→ cycles through every tab including groups; search is scoped to the tab.
    p.Action(20,0);for(int i=0;i<5;++i)SendMessageW(p.window,WM_SYSKEYDOWN,VK_RIGHT,1LL<<29);
    expect(p.tab==clipboard::History::GroupTabBase&&p.visible.size()==2,"Alt+Right reaches first group tab");
    SendMessageW(p.window,WM_SYSKEYDOWN,VK_RIGHT,1LL<<29);expect(p.tab==clipboard::History::GroupTabBase+1&&p.visible.size()==1&&p.visible[0]==first,"second group tab shows its entry");
    SendMessageW(p.window,WM_SYSKEYDOWN,VK_RIGHT,1LL<<29);expect(p.tab==0,"Alt+Right wraps to 全部");
    SendMessageW(p.window,WM_SYSKEYDOWN,VK_LEFT,1LL<<29);expect(p.tab==clipboard::History::GroupTabBase+1,"Alt+Left wraps to last group");
    SendMessageW(p.search,WM_SYSKEYDOWN,VK_LEFT,1LL<<29);expect(p.tab==clipboard::History::GroupTabBase,"Alt+Left works from search box");
    SetWindowTextW(p.search,L"合成分组内容");expect(p.visible.size()==2,"search is scoped to the current group tab");SetWindowTextW(p.search,L"");
    // Screenshots for visual review (synthetic content only).
    auto save=[&](const wchar_t* path,bool dark){p.dark=dark;p.Render();auto frame=MakeFrame({0,0,p.width,p.height});std::copy_n(p.surface->Pixels(),frame.pixels.size(),frame.pixels.begin());for(auto& pixel:frame.pixels){const UINT inverse=255-(pixel>>24);const UINT background=dark?0x151b23:0xf5f8fc;UINT out=0xff000000;for(int shift:{0,8,16})out|=std::min(255u,((pixel>>shift)&255)+((((background>>shift)&255)*inverse+127)/255))<<shift;pixel=out;}SavePng(frame,path);};
    p.Action(20,0);p.status.clear();save(L"build/clipboard-groups-light.png",false);
    right_click(30,second);save(L"build/clipboard-groups-popup-dark.png",true);p.Key(VK_ESCAPE);p.dark=false;
    // Group tab actions: color cycles in place, rename, delete.
    right_click(40,work);expect(p.popup_open&&p.popup_group==work,"right-click group tab opens group actions");
    {const auto before=p.history.FindGroup(work)->color;p.ChoosePopup(popup_index(4,0));expect(p.popup_open&&p.history.FindGroup(work)->color!=before&&p.popup_items[static_cast<size_t>(popup_index(4,0))].dot==p.history.FindGroup(work)->color,"color cycles and popup stays open");}
    p.ChoosePopup(popup_index(3,0));expect(p.naming==2&&search_text()==L"工作","rename prefills current name");
    SetWindowTextW(p.search,L"项目");SendMessageW(p.search,WM_KEYDOWN,VK_RETURN,1);expect(p.history.FindGroup(work)->name==L"项目","rename commits");
    p.SelectTab(clipboard::History::GroupTabBase);right_click(40,work);p.ChoosePopup(popup_index(5,0));
    expect(p.history.groups.size()==1&&p.history.groups[0].id==code&&p.history.Find(second)&&p.history.Find(second)->group==0&&p.history.Find(third)->group==0&&p.tab==0,"deleting the active group keeps its entries and returns to 全部");
    p.SelectTab(clipboard::History::GroupTabBase);expect(p.visible.size()==1&&p.visible[0]==first,"remaining group tab index shifts correctly");
    // Many groups overflow the strip; the active tab is revealed; + refuses past 20.
    while(p.history.groups.size()<clipboard::History::MaxGroups)p.history.AddGroup(L"组 "+std::to_wstring(p.history.groups.size()));
    p.SelectTab(p.TabCount()-1);p.Render();expect(p.tab_scroll_max>0&&p.tab_scroll>0,"tab strip scrolls to reveal the last group");
    {LPARAM at{};expect(center(40,p.history.groups.back().id,at),"revealed last group tab is clickable");}
    click(41,0);expect(!p.naming&&p.status.find(L"20")!=std::wstring::npos,"+ refuses beyond 20 groups");
    {POINT origin{0,0};ClientToScreen(p.window,&origin);const float before=p.tab_scroll;SendMessageW(p.window,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),MAKELPARAM(origin.x+static_cast<int>(100*p.scale),origin.y+static_cast<int>(150*p.scale)));expect(p.tab_scroll<before,"wheel over tab strip scrolls it");}
    // Protected area capacity.
    p.SelectTab(0);for(int i=0;i<static_cast<int>(clipboard::History::MaxProtected);++i){auto e=text(L"合成收藏 "+std::to_wstring(i));e.favorite=true;p.history.Add(e);}
    p.history.Add(text(L"合成普通记录"));p.Filter();const auto plain=p.history.entries.front().id;p.selected=plain;
    hold(true,false);SendMessageW(p.window,WM_KEYDOWN,'2',1);release();
    expect(p.history.Find(plain)->group==0&&p.status.find(L"200")!=std::wstring::npos,"full protected area refuses move into group with a prompt");
    p.Action(31,plain);expect(!p.history.Find(plain)->favorite&&p.status.find(L"200")!=std::wstring::npos,"full protected area refuses new favorite");
    release();p.Enable(false,false);return failures?1:0;
}
static int PinOnly(){
    int failures=0;auto expect=[&](bool ok,const char* what){std::cout<<(ok?"PASS ":"FAIL ")<<what<<std::endl;failures+=!ok;};
    struct Window {HWND h{};~Window(){if(h)DestroyWindow(h);}};
    Window owner{CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Synthetic owner",WS_POPUP,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};
    Window other{CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Synthetic foreground fixture",WS_OVERLAPPEDWINDOW,80,80,640,400,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};
    expect(owner.h&&other.h,"create independent synthetic windows");
    ClipboardPanel panel(owner.h,[]{});auto& p=*panel.impl_;p.test_mode=true;
    expect(p.Enable(true,false),"enable panel without accessing personal clipboard or preferences");
    const auto pump=[](){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE))if(m.message!=WM_QUIT){TranslateMessage(&m);DispatchMessageW(&m);}};
    const auto above=[](HWND front,HWND back){for(HWND w=GetTopWindow(nullptr);w;w=GetWindow(w,GW_HWNDNEXT)){if(w==front)return true;if(w==back)return false;}return false;};
    const auto activate=[&](HWND w){SetActiveWindow(w);pump();expect(GetActiveWindow()==w,"OS activates requested synthetic window");};
    // System client animations on: Show() tries a morph. Settle it like a key press does; if the
    // morph cannot start here, test_mode skips Place()'s final ShowWindow, so do what the product does.
    const auto settle=[&](){if(p.opening)p.StopOpening(true);else if(p.expanded&&!IsWindowVisible(p.window))ShowWindow(p.window,SW_SHOWNOACTIVATE);pump();};
    const auto clickPin=[&](){
        p.Render();for(const auto& hit:p.hits)if(hit.action==10){
            const int x=static_cast<int>((hit.rect.left+hit.rect.right)*.5f*p.scale);
            const int y=static_cast<int>((hit.rect.top+hit.rect.bottom)*.5f*p.scale);
            SendMessageW(p.window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
            SendMessageW(p.window,WM_LBUTTONUP,0,MAKELPARAM(x,y));pump();return;
        }
        expect(false,"pin button has a clickable hit target");
    };
    ShowWindow(other.h,SW_SHOWNOACTIVATE);ShowWindow(p.window,SW_SHOWNOACTIVATE);p.Show();settle();activate(p.window);
    expect(!p.pinned&&p.expanded,"panel starts expanded with pin off");
    activate(other.h);expect(!p.expanded,"unpinned panel folds on actual window deactivation");
    p.Show();settle();activate(p.window);clickPin();expect(p.pinned,"mouse click enables pin");
    p.Render();auto unselected=MakeFrame({0,0,p.width,p.height});std::copy_n(p.surface->Pixels(),unselected.pixels.size(),unselected.pixels.begin());
    for(int i=0;i<5;++i){
        activate(other.h);expect(p.expanded&&IsWindowVisible(p.window),"pinned panel remains expanded while other window is active");
        SetWindowPos(other.h,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        expect((GetWindowLongPtrW(p.window,GWL_EXSTYLE)&WS_EX_TOPMOST)&&above(p.window,other.h),"panel stays above raised ordinary window in actual Z order");
        activate(p.window);
    }
    p.Place();activate(other.h);expect(p.expanded&&above(p.window,other.h),"placement preserves pin and topmost ordering");activate(p.window);
    clickPin();expect(!p.pinned,"second mouse click disables pin");p.Render();
    expect(!std::equal(unselected.pixels.begin(),unselected.pixels.end(),p.surface->Pixels()),"pin state changes rendered button pixels");
    activate(other.h);expect(!p.expanded,"unpin restores automatic folding");
    p.Show();settle();activate(p.window);clickPin();p.Key(VK_ESCAPE);expect(!p.expanded&&p.pinned,"Escape still explicitly folds pinned panel");
    p.Show();settle();expect(p.expanded&&p.pinned,"reopening retains pin within current process");
    p.Action(14,0);expect(!p.expanded,"fold button works while pinned");p.Enable(false,false);
    expect(!p.window&&!p.preview.IsOpen(),"disable destroys pinned panel and preview");return failures?1:0;
}
static int Run(){
    const auto position_file=std::filesystem::temp_directory_path()/(L"lumashot-position-test-"+std::to_wstring(GetCurrentProcessId())+L".ini");
    struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove(path,ec);}}cleanup{position_file};
    int failures=0;auto expect=[&](bool ok,const char* what){std::cout<<(ok?"PASS ":"FAIL ")<<what<<'\n';failures+=!ok;};
    expect(!clipboard::LoadPosition(position_file),"missing layout uses default position");expect(clipboard::SavePosition(position_file,{-1400,230}),"persist negative-monitor position");const auto restored=clipboard::LoadPosition(position_file);expect(restored&&restored->x==-1400&&restored->y==230,"position survives independent load");expect(clipboard::SavePosition(position_file,{950,80}),"atomically overwrite saved position");const auto updated=clipboard::LoadPosition(position_file);expect(updated&&updated->x==950&&updated->y==80,"updated position restored");
    expect(clipboard::SavePosition(position_file,{950,80},SIZE{520,960}),"save expanded dimensions in DIP");
    expect(clipboard::SavePosition(position_file,{920,90}),"moving folded panel preserves expanded size");
    const auto dimensions=clipboard::LoadPanelSize(position_file);expect(dimensions&&dimensions->cx==520&&dimensions->cy==960,"expanded dimensions survive position save and reload");
    WritePrivateProfileStringW(L"Panel",L"Width",L"-1",position_file.c_str());expect(!clipboard::LoadPanelSize(position_file),"invalid dimensions fall back to default");
    ClipboardPanel panel(nullptr,[]{});auto& p=*panel.impl_;p.test_mode=true;expect(p.Enable(true,false),"create real panel with clipboard monitoring disabled");
    DWORD affinity=0xffffffff;expect(GetWindowDisplayAffinity(p.window,&affinity)&&affinity==WDA_NONE,"clipboard window permits screen capture");
    for(float dpi_scale:{1.f,1.25f,1.5f,2.f}){p.scale=dpi_scale;p.LayoutSearch();RECT bounds{},format{};GetWindowRect(p.search,&bounds);MapWindowPoints(nullptr,GetParent(p.search),reinterpret_cast<POINT*>(&bounds),2);SendMessageW(p.search,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));HDC dc=GetDC(p.search);const auto old=SelectObject(dc,p.font);TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);SelectObject(dc,old);ReleaseDC(p.search,dc);expect(std::abs(bounds.top+format.top+metrics.tmHeight/2.f-98.5f*dpi_scale)<=.51f,"native search text centered at each DPI");expect(bounds.top>=76*dpi_scale&&bounds.bottom<=121*dpi_scale,"native search control stays inside rounded field");}p.scale=1;p.LayoutSearch();
    p.expanded=true;p.Place();p.Render();expect(p.text_glyphs>0,"panel text rasterizes through LumaText FreeType");
    for(const auto band:{std::pair{330,366},std::pair{373,417}}){int left=p.width,right=-1;for(int y=int(band.first*p.scale);y<int(band.second*p.scale);++y)for(int x=0;x<p.width;++x){const auto pixel=p.surface->Pixels()[size_t(y)*p.width+x];if(((pixel>>16)&255)<160&&((pixel>>8)&255)<160&&(pixel&255)<180){left=std::min(left,x);right=std::max(right,x);}}expect(right>=left&&std::abs((left+right)/2.f-p.width/2.f)<3*p.scale,"empty-state line is centered across the panel");}
    {auto frame=MakeFrame({0,0,p.width,p.height});for(size_t i=0;i<frame.pixels.size();++i){const auto source=p.surface->Pixels()[i],alpha=source>>24;uint32_t pixel=0xff000000;for(int shift:{0,8,16})pixel|=std::min(255u,((source>>shift)&255)+245*(255-alpha)/255)<<shift;frame.pixels[i]=pixel;}SavePng(frame,L"build/clipboard-empty-centered.png");}
    {const auto render=[&](const std::wstring& value){p.target->BeginDraw();p.target->Clear(D2D1::ColorF(0xffffff));p.TextBlock(value,{20,20,380,74},13,0x223248);p.target->EndDraw();std::vector<uint32_t> pixels;for(int y=int(20*p.scale);y<int(38*p.scale);++y)for(int x=int(20*p.scale);x<int(380*p.scale);++x)pixels.push_back(p.surface->Pixels()[size_t(y)*p.width+x]);return pixels;};
     const auto shortText=render(L"首行内容 FIRST LINE");const auto longText=render(L"首行内容 FIRST LINE\n第二行内容 SECOND LINE\n第三行内容 THIRD LINE\n第四行内容 FOURTH LINE\n末尾内容 LAST LINE");expect(shortText==longText,"overflow text preserves first line at top instead of centering clipped content");}
    const wchar_t* texts[]={L"产品需求文档 v1.0.pdf",L"图片",L"https://www.microsoft.com/zh-cn",L"const createBetter = () => {\n   return \"A more productive tomorrow\";\n};",L"高效的工作方式，源于对细节的坚持。\n优秀的产品，始于对用户的理解。"};
    for(int i=0;i<5;++i){clipboard::Entry e;e.kind=i==0?clipboard::Kind::Files:i==1?clipboard::Kind::Image:clipboard::Kind::Text;e.text=texts[i];e.time={2026,9,5,18,14,static_cast<WORD>(10+i*4),0,0};e.formats.push_back({CF_UNICODETEXT,{static_cast<unsigned char>(i)}});
        if(i==1){BITMAPINFOHEADER h{};h.biSize=sizeof(h);h.biWidth=120;h.biHeight=-80;h.biPlanes=1;h.biBitCount=32;h.biCompression=BI_RGB;std::vector<unsigned char> dib(sizeof(h)+120*80*4);std::memcpy(dib.data(),&h,sizeof(h));for(int y=0;y<80;++y)for(int x=0;x<120;++x){const uint32_t color=y<40?0xff83b9e4u:0xff326a91u;std::memcpy(dib.data()+sizeof(h)+(y*120+x)*4,&color,4);}e.formats={{CF_DIB,std::move(dib)}};}
        p.history.Add(std::move(e));}
    p.hotkey=true;p.status.clear();p.Filter();expect(p.visible.size()==5,"five synthetic rows");p.Action(31,4);expect(p.history.Find(4)->favorite,"favorite action");p.selected=4;
    const auto snapshot=[&](const wchar_t* path,bool expanded,bool dark,float scale){p.expanded=expanded;p.dark=dark;p.scale=scale;SetWindowPos(p.window,nullptr,0,0,static_cast<int>((expanded?p.W:p.CW)*scale),static_cast<int>((expanded?p.H:96)*scale),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);p.Render();expect(p.file_icons->WaitIdle(10000),"file icon worker finishes for synthetic snapshot");p.Render();auto frame=MakeFrame({0,0,p.width,p.height});std::copy_n(p.surface->Pixels(),frame.pixels.size(),frame.pixels.begin());for(auto& pixel:frame.pixels)pixel|=0xff000000;SavePng(frame,path);expect(p.surface!=nullptr,"render production surface");};
    snapshot(L"clipboard-expanded-light.png",true,false,1);expect(p.width==400,"expanded width is 400 DIP");for(const auto& hit:p.hits)if(hit.action>=31&&hit.action<=34)expect(hit.rect.right-hit.rect.left==32&&hit.rect.bottom-hit.rect.top==32,"smaller action graphics retain 32 DIP click targets");D2D1_MATRIX_3X2_F transform{};p.target->GetTransform(&transform);expect(transform._11==1&&transform._22==1&&transform._31==0&&transform._32==0,"icon transforms do not affect following content");snapshot(L"clipboard-expanded-dark-150.png",true,true,1.5f);snapshot(L"clipboard-folded.png",false,false,2);
    p.expanded=true;p.scale=1;p.hover=-1;SetWindowPos(p.window,nullptr,0,0,400,744,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);p.Render();
    for(const auto& action:p.hits)if(action.action>=31&&action.action<=34){const auto card=std::find_if(p.hits.begin(),p.hits.end(),[&](const auto& h){return h.action==30&&h.id==action.id;});expect(card!=p.hits.end()&&action.rect.right<=card->rect.right-4&&action.rect.bottom<=card->rect.bottom-4,"row action hit areas stay inset from card border");}
    const auto button=std::find_if(p.hits.begin(),p.hits.end(),[](const auto& h){return h.action==34;});
    const auto hover_card=std::find_if(p.hits.begin(),p.hits.end(),[&](const auto& h){return h.action==30&&h.id==button->id;})->rect;
    const std::vector<uint32_t> baseline(p.surface->Pixels(),p.surface->Pixels()+size_t(p.width)*p.height);
    p.hover=static_cast<int>(button-p.hits.begin());p.Render();bool contained=true;
    for(int py=0;py<p.height;++py)for(int px=0;px<p.width;++px){const float fx=px+.5f,fy=py+.5f;const float cx=std::clamp(fx,hover_card.left+13,hover_card.right-13),cy=std::clamp(fy,hover_card.top+13,hover_card.bottom-13);if((fx-cx)*(fx-cx)+(fy-cy)*(fy-cy)>15*15&&baseline[size_t(py)*p.width+px]!=p.surface->Pixels()[size_t(py)*p.width+px])contained=false;}
    expect(contained,"delete hover cannot change pixels outside rounded card");snapshot(L"clipboard-delete-hover-fixed.png",true,false,1);p.hover=-1;
    expect(p.H==744,"embedded content preview removed and panel height reduced");
    p.selected=2;snapshot(L"clipboard-image-preview-light.png",true,false,1);snapshot(L"clipboard-image-preview-dark.png",true,true,1);p.selected=4;
    const auto all_entries=p.history.entries;
    p.history.entries.clear();for(const auto* name:{L"项目资料.docx",L"成本汇总.xlsx",L"交付文件.zip",L"产品说明.PDF",L"未知类型.lumashot_unknown"}){clipboard::Entry entry;entry.kind=clipboard::Kind::Files;entry.text=name;entry.formats.push_back({CF_HDROP,{static_cast<unsigned char>(p.history.entries.size()+30)}});p.history.Add(std::move(entry));}p.Filter();
    snapshot(L"clipboard-system-icons-light.png",true,false,1);snapshot(L"clipboard-system-icons-dark-200.png",true,true,2);expect(p.file_bitmaps.size()>=10,"real shell icons cached for every file row at both DPI buckets");
    p.history.entries=all_entries;p.Filter();
    const auto saved_entries=p.history.entries;p.history.entries.resize(1);snapshot(L"clipboard-folded-one.png",false,false,1);p.history.entries.resize(100);snapshot(L"clipboard-folded-hundred.png",false,false,2);p.history.entries=saved_entries;
    expect(p.width==56&&p.height==192,"compact folded dimensions are 28 by 96 DIP at 200 percent");
    p.expanded=true;p.Place();p.OpenMore();expect(p.menu_open&&!IsWindowVisible(p.search),"themed menu hides overlapping native search child");snapshot(L"clipboard-menu-light.png",true,false,1);snapshot(L"clipboard-menu-dark-150.png",true,true,1.5f);p.Key(VK_ESCAPE);expect(!p.menu_open&&p.expanded,"Escape dismisses menu without folding panel");
    const auto menu_entries=p.history.entries;p.OpenMore();p.MenuKey(VK_UP);expect(p.menu_hover==2,"keyboard navigation wraps to final action");p.MenuKey(VK_RETURN);expect(p.menu_confirm&&p.menu_hover==0,"clear all requires confirmation with cancel selected");snapshot(L"clipboard-menu-confirm.png",true,false,1);p.MenuKey(VK_RETURN);expect(!p.menu_open&&p.history.entries.size()==menu_entries.size(),"default confirmation action cancels without clearing");
    p.OpenMore();SendMessageW(p.window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(4,4));expect(!p.menu_open&&p.expanded,"outside click dismisses without click-through");p.OpenMore();p.ChooseMore(1);expect(p.history.entries.size()==1&&p.history.entries[0].favorite,"clear unstarred preserves favorites");p.history.entries=menu_entries;p.Filter();p.OpenMore();p.ChooseMore(2);p.ChooseMore(1);expect(p.history.entries.empty(),"explicit themed confirmation clears all");p.OpenMore();expect(!p.MenuEnabled(1)&&!p.MenuEnabled(2),"empty-history destructive actions disabled");p.CloseMore();p.history.entries=menu_entries;p.Filter();
    p.expanded=false;p.Place();RECT before{};GetWindowRect(p.window,&before);const POINT origin{before.left+10,before.top+10};p.BeginDrag(origin);p.Drag({origin.x-80,origin.y+35});expect(p.dragged&&!p.expanded,"folded drag moves without expanding");p.EndDrag();RECT moved{};GetWindowRect(p.window,&moved);expect(moved.left!=before.left||moved.top!=before.top,"folded window position changes");const POINT anchor=p.anchor;p.Place();RECT stable{};GetWindowRect(p.window,&stable);expect(stable.left==moved.left&&stable.top==moved.top,"layout retains dragged position");p.expanded=true;p.Place();p.Fold();expect(p.anchor.x==anchor.x&&p.anchor.y==anchor.y,"expand fold retains common anchor");
    p.expanded=true;p.Place();GetWindowRect(p.window,&before);p.BeginDrag({before.left+60,before.top+25});p.Drag({before.left+100,before.top+65});expect(p.dragged,"expanded title drag uses movement threshold");p.EndDrag();
    p.BeginDrag({0,0});p.Drag({100000,100000});p.EndDrag();GetWindowRect(p.window,&moved);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(p.window,MONITOR_DEFAULTTONEAREST),&monitor);expect(moved.left>=monitor.rcWork.left&&moved.top>=monitor.rcWork.top&&moved.right<=monitor.rcWork.right&&moved.bottom<=monitor.rcWork.bottom,"drag clamps panel to monitor work area");
    for(float dpi:{1.f,1.5f,2.f}){int edge=0;expect(clipboard::SnapAxis(-1000+static_cast<LONG>(20*dpi),-1000,-200,dpi,edge)==-1000&&edge==-1,"negative-monitor edge snaps at every DPI");expect(clipboard::SnapAxis(-1000+static_cast<LONG>(30*dpi),-1000,-200,dpi,edge)==-1000,"magnet hysteresis prevents edge jitter");expect(clipboard::SnapAxis(-1000+static_cast<LONG>(40*dpi),-1000,-200,dpi,edge)>-1000&&edge==0,"dragging away releases magnet");}
    p.expanded=false;p.Place();GetWindowRect(p.window,&before);GetMonitorInfoW(MonitorFromWindow(p.window,MONITOR_DEFAULTTONEAREST),&monitor);
    const LONG fw=before.right-before.left,fh=before.bottom-before.top;const auto work=monitor.rcWork;
    for(const POINT desired:{POINT{work.left+12,work.top+100},POINT{work.right-fw-12,work.top+100},POINT{work.left+100,work.top+12},POINT{work.left+100,work.bottom-fh-12}}){
        GetWindowRect(p.window,&before);p.BeginDrag({before.left+10,before.top+10});p.Drag({desired.x+10,desired.y+10});p.EndDrag();GetWindowRect(p.window,&moved);
        expect(moved.left==work.left||moved.right==work.right||moved.top==work.top||moved.bottom==work.bottom,"folded native drag snaps to each work-area edge");
        p.Place();GetWindowRect(p.window,&stable);expect(EqualRect(&moved,&stable)!=FALSE,"snapped position survives relayout");
    }
    p.expanded=true;p.Place();
    p.Action(24,0);expect(p.visible.size()==1&&p.visible[0]==4,"favorite tab");p.Action(20,0);SetWindowTextW(p.search,L"MICROSOFT");expect(p.visible.size()==1,"native edit search updates results");SetWindowTextW(p.search,L"");p.Action(22,0);expect(p.visible.size()==1,"image category");
    const auto source=clipboard::Thumbnail(*p.history.Find(2));expect(source!=nullptr,"synthetic DIB thumbnail decodes through WIC");
    const auto retained=p.history.entries.size();p.Fold();panel.SetHotkeysSuspended(true);
    SendMessageW(p.window,WM_HOTKEY,1,0);expect(!p.expanded&&!p.hotkey&&p.enabled&&p.listening&&p.history.entries.size()==retained,"suspension blocks queued clipboard hotkey without stopping history collection");
    panel.Show();expect(p.expanded,"clipboard remains manually accessible while hotkeys are disabled");p.Fold();panel.SetHotkeysSuspended(false);
    // Probe the permission gate using a synthetic open-mini state, not the
    // user's foreground edit/focus. Dedicated shortcut tests cover all routes.
    p.quick_active=true;
    p.hotkey_allowed=[] {return false;};SendMessageW(p.window,WM_HOTKEY,1,0);expect(!p.expanded&&p.quick_active,"live game guard blocks clipboard hotkey before next policy timer");
    p.hotkey_allowed=[] {return true;};SendMessageW(p.window,WM_HOTKEY,1,0);expect(!p.quick_active,"allowed clipboard shortcut reaches dispatcher and closes synthetic mini state");
    p.pinned=true;p.expanded=true;const auto kept_scroll=p.scroll;const auto kept_query=p.query;
    p.last_read_id=0;p.Action(30,4);expect(p.last_read_id==4&&p.expanded,"continuous mode single-click inserts first synthetic item without folding");
    p.Action(30,3);expect(p.last_read_id==3&&p.expanded&&p.scroll==kept_scroll&&p.query==kept_query,"continuous second insertion preserves viewport and search");
    p.pinned=false;p.last_read_id=0;p.Action(30,4);expect(!p.last_read_id,"normal mode row click still selects without pasting");
    {HWND sink=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,1,1,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);clipboard::WinVShortcut hook;bool cycled=true;
     for(int i=0;i<20&&cycled;++i){cycled=hook.Install(sink)&&hook.Installed()&&hook.HookThreadId()!=0&&hook.HookThreadId()!=GetCurrentThreadId();hook.Reset();cycled=cycled&&!hook.Installed()&&!hook.HookThreadId();}
     expect(cycled,"Win+V hook installs on a dedicated thread and joins cleanly on reset");expect(!hook.Install(nullptr)&&!hook.Installed(),"Win+V hook refuses a missing target window");
     expect(hook.Install(sink),"Win+V hook reinstalls after refusal");DestroyWindow(sink);}
    {clipboard::WinVChord chord;bool trigger{};
     expect(!chord.Consume('A',true,true,false,true,trigger),"Win+V handler leaves other keys untouched");
     expect(!chord.Consume('V',true,true,true,true,trigger),"Win+V handler leaves modified chords untouched");
     expect(!chord.Consume('V',true,true,false,false,trigger),"suspended handler lets system Win+V pass");
     expect(chord.Consume('V',true,true,false,true,trigger)&&trigger,"Win+V launches once on keydown");
     expect(chord.Consume('V',true,true,false,true,trigger)&&!trigger,"held Win+V does not autorepeat");
     expect(chord.Consume('V',false,false,false,true,trigger)&&!trigger&&!chord.held,"matching keyup is swallowed even if Win was released first");}
    p.test_mode=false;
    const UINT synthetic_mods=MOD_CONTROL|MOD_ALT|MOD_SHIFT;
    expect(panel.SetShortcut(synthetic_mods,VK_F23)&&p.hotkey,"custom clipboard shortcut registers");
    expect(panel.SetShortcut(MOD_WIN,'V')&&p.hotkey,"reserved Win+V installs dedicated hook");
    expect(panel.SetShortcut(synthetic_mods,VK_F23)&&p.hotkey,"changing away from Win+V restores ordinary registration");
    expect(!RegisterHotKey(p.window,77,synthetic_mods,VK_F23),"configured chord is owned by clipboard");
    expect(RegisterHotKey(p.window,77,synthetic_mods,VK_F24)!=FALSE,"synthetic external conflict registered");
    expect(!panel.SetShortcut(synthetic_mods,VK_F24)&&p.shortcut_key==VK_F23&&p.hotkey,"conflicting shortcut rolls back previous binding");
    UnregisterHotKey(p.window,77);
    expect(panel.SetShortcut(synthetic_mods,0)&&!p.hotkey,"clearing releases clipboard shortcut");
    expect(RegisterHotKey(p.window,77,synthetic_mods,VK_F23)!=FALSE,"old clipboard chord is free after clear");UnregisterHotKey(p.window,77);
    p.Fold();SendMessageW(p.window,WM_HOTKEY,1,0);expect(!p.expanded,"cleared shortcut ignores queued dispatch");
    p.test_mode=true;
    p.ClosePreview();p.expanded=true;p.tab=0;p.Filter();p.scale=1;
    const auto resize_entries=p.history.entries;
    for(int i=0;i<12;++i){clipboard::Entry row;row.kind=clipboard::Kind::Text;row.text=L"Synthetic resize row "+std::to_wstring(i);row.formats={{CF_UNICODETEXT,{static_cast<unsigned char>(i+80)}}};p.history.Add(std::move(row));}p.Filter();
    for(const SIZE size:{SIZE{400,480},SIZE{520,960},SIZE{400,744}}){
        SendMessageW(p.window,WM_ENTERSIZEMOVE,0,0);
        SetWindowPos(p.window,nullptr,0,0,size.cx,size.cy,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        SendMessageW(p.window,WM_EXITSIZEMOVE,0,0);p.Render();
        expect(std::abs(p.W-size.cx)<1&&std::abs(p.H-size.cy)<1,"native resize updates DIP layout without changing font scale");
        int rows=0;for(const auto& hit:p.hits)if(hit.action==30){++rows;expect(hit.rect.bottom<=p.ListBottom+.1f,"resized rows stay above footer");}
        expect(rows==p.PageRows(),"resized list fills adaptive page capacity");
        p.scroll=100;SendMessageW(p.window,WM_MOUSEWHEEL,MAKEWPARAM(0,0),0);expect(p.scroll==static_cast<int>(p.visible.size())-p.PageRows(),"resized scrollbar clamps to current page size");
        RECT bounds{};GetWindowRect(p.window,&bounds);
        const auto hit=[&](LONG x,LONG y){return SendMessageW(p.window,WM_NCHITTEST,0,MAKELPARAM(x,y));};
        expect(hit(bounds.left+1,bounds.top+1)==HTTOPLEFT&&hit(bounds.right-1,bounds.bottom-1)==HTBOTTOMRIGHT&&hit(bounds.right-1,(bounds.top+bounds.bottom)/2)==HTRIGHT,"native edges and corners expose resize cursors");
        if(size.cx==520){auto frame=MakeFrame({0,0,p.width,p.height});for(size_t i=0;i<frame.pixels.size();++i){const auto pixel=p.surface->Pixels()[i],inverse=255-(pixel>>24);uint32_t out=0xff000000;for(UINT shift:{0u,8u,16u})out|=std::min(255u,((pixel>>shift)&255)+245*inverse/255)<<shift;frame.pixels[i]=out;}SavePng(frame,L"clipboard-resized.png");}
    }
    p.history.entries=resize_entries;p.Filter();p.Fold();RECT compact{};GetWindowRect(p.window,&compact);
    const auto compact_core=p.VisualBodyBounds();expect(p.ResizeHit({(compact_core.left+compact_core.right)/2,(compact_core.top+compact_core.bottom)/2})==HTCLIENT&&std::abs((compact_core.right-compact_core.left)/p.scale-p.CW)<1,"folded liquid body keeps its size and cannot be resized");
    p.expanded=true;p.Place();expect(p.W==400&&p.H==744,"folding preserves expanded dimensions");
    p.Action(20,0);p.Action(34,3);expect(!p.history.Find(3),"delete action");p.Enable(false,false);expect(p.history.entries.empty()&&!p.enabled&&!IsWindowVisible(p.window)&&!p.file_icons&&p.file_bitmaps.empty(),"disable destroys panel and releases history");
    const auto store_root=std::filesystem::temp_directory_path()/(L"lumashot-panel-store-test-"+std::to_wstring(GetCurrentProcessId()));
    struct StoreCleanup{std::filesystem::path path;~StoreCleanup(){std::error_code ec;std::filesystem::remove_all(path,ec);}}store_cleanup{store_root};
    expect(p.Enable(true,false),"re-enable isolated panel without system clipboard listener");p.disk=std::make_unique<clipboard::SessionStore>(store_root);p.disk->WaitIdle();
    for(int i=0;i<12;++i){clipboard::Entry entry;entry.kind=clipboard::Kind::Text;entry.text=std::wstring(2000,L'测')+L" keyboard-tail-"+std::to_wstring(i);const auto* bytes=reinterpret_cast<const unsigned char*>(entry.text.c_str());entry.formats.push_back({CF_UNICODETEXT,{bytes,bytes+(entry.text.size()+1)*2}});expect(p.disk->Save(std::move(entry)),"queue isolated keyboard fixture");}
    expect(p.disk->WaitIdle(),"keyboard fixture stored asynchronously");p.DrainStore();p.tab=0;p.query.clear();SetWindowTextW(p.search,L"");p.expanded=true;p.Filter();p.selected=p.visible.front();
    const auto first=p.selected;const auto jobs_before=p.disk->Stats().jobs;
    for(int i=0;i<7;++i)SendMessageW(p.search,WM_KEYDOWN,VK_DOWN,0);
    expect(p.selected==p.visible[7]&&p.scroll==3,"Down navigates results from search edit and scrolls selection into view");SendMessageW(p.search,WM_KEYDOWN,VK_UP,0);expect(p.selected==p.visible[6],"Up also navigates while typing focus stays in search");expect(p.disk->Stats().jobs==jobs_before,"arrow selection performs no payload disk reads");
    const auto expected_id=p.selected;SendMessageW(p.search,WM_KEYDOWN,VK_RETURN,0);expect(p.copy_pending,"Enter queues selected payload without blocking");expect(p.disk->WaitIdle(),"selected payload read completes");p.DrainStore();expect(p.last_read_id==expected_id&&!p.copy_pending,"Enter reaches synthetic paste sink with correct entry");
    p.last_read_id=0;p.Copy(first,true);p.Fold();expect(p.disk->WaitIdle(),"cancelled paste worker settles");p.DrainStore();expect(p.last_read_id==0&&!p.copy_pending,"fold cancels delayed paste without touching clipboard");
    p.expanded=true;SetWindowTextW(p.search,L"KEYBOARD-TAIL-11");expect(p.disk->WaitIdle(),"background full-text search completes");p.DrainStore();expect(p.visible.size()==1,"search reaches text beyond bounded metadata summary");
    SetWindowTextW(p.search,L"keyboard-tail-2");SetWindowTextW(p.search,L"keyboard-tail-7");expect(p.disk->WaitIdle(),"superseded searches settle");p.DrainStore();expect(p.visible.size()==1&&p.history.Find(p.visible.front())->id!=first,"stale search result cannot overwrite latest query");
    // Exercise the real TranslateMessage -> WM_KEYDOWN -> WM_CHAR sequence,
    // not only SendMessage(WM_KEYDOWN), which missed the original space bug.
    const auto pump=[](){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE))if(m.message!=WM_QUIT){TranslateMessage(&m);DispatchMessageW(&m);}};
    const auto press=[&](HWND recipient,WPARAM key,LPARAM flags=1){
        MSG m{};m.hwnd=recipient;m.message=WM_KEYDOWN;m.wParam=key;m.lParam=flags;
        TranslateMessage(&m);DispatchMessageW(&m);pump();
        SendMessageW(recipient,WM_KEYUP,key,(1LL<<30)|(1LL<<31)|1);
    };
    ShowWindow(p.window,SW_SHOWNOACTIVATE);p.Show();
    expect(GetFocus()==p.window,"opening clipboard focuses list, not the search input");
    press(p.window,VK_SPACE);
    expect(p.preview.IsOpen()&&p.query.empty()&&GetFocus()==p.window,"real Space toggles preview without adding query whitespace or moving focus");
    expect(p.disk->WaitIdle(),"text preview payload loads");p.DrainStore();
    expect(clipboard::PreviewWindowTest::Text(p.preview).find(L"keyboard-tail-")!=std::wstring::npos,"preview reads full text beyond metadata summary");
    press(p.window,VK_SPACE,(1LL<<30)|1);
    expect(p.preview.IsOpen(),"held Space repeat does not oscillate preview");
    press(p.window,VK_SPACE);expect(!p.preview.IsOpen()&&p.query.empty(),"second Space closes cleanly");
    SetFocus(p.search);press(p.search,VK_SPACE);
    expect(p.preview.IsOpen()&&p.query.empty()&&GetFocus()==p.window,"empty search Space consumes the translated character and returns list focus");
    press(p.window,VK_ESCAPE);expect(!p.preview.IsOpen()&&p.expanded,"first Escape closes preview only");
    SetFocus(p.search);SetWindowTextW(p.search,L"keyboard");SendMessageW(p.search,EM_SETSEL,8,8);
    press(p.search,VK_SPACE);expect(p.query==L"keyboard "&&!p.preview.IsOpen(),"spaces remain editable inside a deliberate nonempty search");
    expect(p.disk->WaitIdle(),"space query completes");p.DrainStore();
    SetWindowTextW(p.search,L"keyboard-tail");expect(p.disk->WaitIdle(),"nonempty search completes");p.DrainStore();
    press(p.search,VK_DOWN);expect(GetFocus()==p.window&&!p.query.empty(),"arrow navigation transfers search focus to result list");
    press(p.window,VK_SPACE);expect(p.preview.IsOpen()&&p.query==L"keyboard-tail","Space previews filtered results without modifying nonempty query");
    press(p.window,VK_ESCAPE);press(p.window,VK_ESCAPE);expect(!p.expanded&&!p.preview.IsOpen(),"second Escape folds panel and releases preview");
    p.Show();press(p.window,'K');expect(GetFocus()==p.search&&(p.query==L"k"||p.query==L"K"),"typing from list still starts search");
    press(p.search,VK_TAB);expect(GetFocus()==p.window,"Tab returns from search to list");
    press(p.window,VK_TAB);expect(GetFocus()==p.search,"Tab opens search from list");
    p.Fold();ShowWindow(p.window,SW_HIDE);
    SetWindowTextW(p.search,L"");p.history.Clear(false);p.disk->WaitIdle();p.DrainStore();
    for(int i=0;i<12;++i){clipboard::Entry entry;entry.kind=clipboard::Kind::Image;entry.text=L"synthetic image "+std::to_wstring(i);BITMAPINFOHEADER header{};header.biSize=sizeof(header);header.biWidth=512;header.biHeight=-512;header.biPlanes=1;header.biBitCount=32;header.biCompression=BI_RGB;std::vector<unsigned char> bytes(sizeof(header)+512*512*4);std::memcpy(bytes.data(),&header,sizeof(header));const uint32_t color=0xff506080u+static_cast<uint32_t>(i);for(size_t j=sizeof(header);j<bytes.size();j+=4)std::memcpy(bytes.data()+j,&color,4);entry.formats.push_back({CF_DIB,std::move(bytes)});expect(p.disk->Save(std::move(entry)),"queue valid synthetic DIB");}
    expect(p.disk->WaitIdle(),"valid image fixtures written");p.DrainStore();p.expanded=true;p.scroll=0;p.Filter();p.Render();expect(p.disk->WaitIdle(),"visible thumbnails decode on worker");p.DrainStore();p.Render();size_t bitmap_bytes=0;for(const auto& [image_id,bitmap]:p.images){(void)image_id;if(bitmap){const auto size=bitmap->GetPixelSize();bitmap_bytes+=size.width*size.height*4;}}
    expect(bitmap_bytes>0&&bitmap_bytes<=6*128*128*4&&p.images.size()<=6,"only visible thumbnails retained within 384 KiB pixel budget");
    expect(p.images.size()==5&&bitmap_bytes==5*128*128*4,"every visible image has a decoded thumbnail");
    p.images.clear();p.thumbnail_cache.Clear();p.Render();
    expect(p.disk->WaitIdle(),"thumbnail reload completes before target loss");
    p.images.clear();p.file_bitmaps.clear();p.app_icon.Reset();p.brush.Reset();p.target.Reset();
    p.DrainStore();p.Render();
    expect(p.images.size()==5&&p.thumbnail_pending.empty(),"all thumbnails survive render target loss during delivery");

    for(int i=0;i<10;++i){p.Key(VK_DOWN);p.Render();p.disk->WaitIdle();p.DrainStore();}
    expect(p.images.size()<=6,"scrolling many images never accumulates prior image bitmaps");
    // End-to-end metadata -> encrypted payload -> background decode -> Quick Look.
    p.selected=p.visible.front();p.ShowPreview();
    expect(clipboard::PreviewWindowTest::Loading(p.preview)||clipboard::PreviewWindowTest::HasImage(p.preview),"disk image opens with cached pixels, thumbnail or explicit loading state");
    expect(p.history.Find(p.selected)->formats.empty(),"production-like history contains metadata only");
    expect(p.disk->WaitIdle(),"disk preview decode completes");p.DrainStore();
    expect(!clipboard::PreviewWindowTest::Loading(p.preview)&&clipboard::PreviewWindowTest::HasImage(p.preview),"disk-backed image actually reaches Quick Look");
    {auto pixels=clipboard::PreviewWindowTest::Snapshot(p.preview);expect(!pixels.pixels.empty(),"initial preview renderer is materialized");}
    const auto preview_window=clipboard::PreviewWindowTest::Window(p.preview);
    const auto preview_renderer=clipboard::PreviewWindowTest::Renderer(p.preview);
    const auto first_image=p.selected;p.Key(VK_DOWN);const auto second_image=p.selected;
    expect(first_image!=second_image&&p.preview.CurrentId()==second_image,"open preview follows selection immediately");
    expect(p.disk->WaitIdle(),"selection decode finishes");p.DrainStore();
    expect(clipboard::PreviewWindowTest::HasImage(p.preview)&&p.preview.CurrentId()==second_image,"selected image wins over stale completions");
    {auto pixels=clipboard::PreviewWindowTest::Snapshot(p.preview);expect(!pixels.pixels.empty(),"replacement preview paints");}
    expect(clipboard::PreviewWindowTest::Window(p.preview)==preview_window&&clipboard::PreviewWindowTest::Renderer(p.preview)==preview_renderer,
           "switching images reuses the window and render target while replacing content");
    for(int i=0;i<24;++i)p.Action(30,p.visible[static_cast<size_t>(i%2)]);
    const auto rapid_id=p.selected;expect(p.disk->WaitIdle(),"rapid preview selection requests settle");p.DrainStore();
    expect(p.preview.CurrentId()==rapid_id&&clipboard::PreviewWindowTest::HasImage(p.preview),"only latest generation appears after rapid selection changes");
    p.ClosePreview();p.ShowPreview();p.Key(VK_SPACE);
    expect(!p.preview.IsOpen(),"Space hides a preview while its decode is pending");
    expect(p.disk->WaitIdle(),"hidden decode finishes");p.DrainStore();
    expect(!p.preview.IsOpen()&&p.preview_cache.Find(p.selected),"hidden completion warms cache without reopening window");
    p.Key(VK_SPACE);
    expect(p.preview.IsOpen()&&!clipboard::PreviewWindowTest::Loading(p.preview)&&clipboard::PreviewWindowTest::HasImage(p.preview),"Space immediately reopens completed hidden preview");
    p.ClosePreview();p.ShowPreview();expect(p.disk->WaitIdle(),"queued close fixture completes before drain");p.ClosePreview();p.DrainStore();
    expect(clipboard::PreviewWindowTest::Released(p.preview),"late completed load cannot reopen a closed preview");
    p.ShowPreview();p.Fold();expect(p.disk->WaitIdle(),"fold cancels preview read");p.DrainStore();
    expect(clipboard::PreviewWindowTest::Released(p.preview)&&p.disk->Stats().result_bytes==0,"fold frees preview pixels and discards in-flight completion");
    p.expanded=true;p.Place();p.Filter();p.ShowPreview();SetWindowTextW(p.search,L"no-such-synthetic-image");
    expect(!p.preview.IsOpen(),"filter with no selection closes obsolete preview");
    expect(p.disk->WaitIdle(),"empty filter settles");p.DrainStore();SetWindowTextW(p.search,L"");
    // 4K PNG is small on disk but exceeds the old 4M source-pixel thumbnail gate.
    const auto png_path=store_root/L"synthetic-4k.png";
    {auto frame=MakeFrame({0,0,3840,2160});std::fill(frame.pixels.begin(),frame.pixels.end(),0xff4080a0);SavePng(frame,png_path);}
    clipboard::Entry png;png.kind=clipboard::Kind::Image;png.text=L"synthetic 4K PNG";
    {std::ifstream file(png_path,std::ios::binary);std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),{});png.formats.push_back({RegisterClipboardFormatW(L"PNG"),std::move(bytes)});}
    expect(p.disk->Save(std::move(png))&&p.disk->WaitIdle(),"compressed synthetic 4K image stored without using real clipboard");p.DrainStore();p.selected=p.visible.front();p.ShowPreview();
    expect(p.disk->WaitIdle(),"4K background decode completes");p.DrainStore();
    expect(clipboard::PreviewWindowTest::RenderedImage(p.preview),"4K PNG from encrypted history renders correct actual pixels");
    SavePng(clipboard::PreviewWindowTest::Snapshot(p.preview),L"clipboard-quicklook-fixed.png");
    p.ClosePreview();p.ShowPreview();p.Copy(p.selected,false);
    expect(p.disk->WaitIdle(),"copy and preview jobs finish independently");p.DrainStore();
    expect(clipboard::PreviewWindowTest::HasImage(p.preview)&&p.last_read_id==p.selected,"preview and paste/copy tokens cannot consume one another");
    const auto corrupt_id=p.selected;const auto corrupt_key=p.history.Find(corrupt_id)->payload->key;
    p.ClosePreview();{std::ofstream corrupt(p.disk->Directory()/(std::to_wstring(corrupt_key)+L".bin"),std::ios::binary|std::ios::trunc);corrupt<<"synthetic corrupted encrypted payload";}
    p.ShowPreview();expect(p.disk->WaitIdle(),"corrupt payload read finishes");p.DrainStore();
    expect(!clipboard::PreviewWindowTest::Loading(p.preview)&&!clipboard::PreviewWindowTest::Error(p.preview).empty(),"unreadable encrypted payload ends loading with explicit error");
    p.Key(VK_DELETE);expect(!p.preview.IsOpen()&&!p.history.Find(corrupt_id),"delete cancels preview and removes selected entry");
    clipboard::Entry broken;broken.kind=clipboard::Kind::Image;broken.text=L"synthetic invalid image";broken.formats.push_back({CF_DIB,{1,2,3,4}});
    expect(p.disk->Save(std::move(broken))&&p.disk->WaitIdle(),"store unsupported image fixture");p.DrainStore();p.selected=p.visible.front();p.ShowPreview();
    expect(p.disk->WaitIdle(),"invalid image decode finishes");p.DrainStore();
    expect(!clipboard::PreviewWindowTest::Loading(p.preview)&&!clipboard::PreviewWindowTest::Error(p.preview).empty(),"invalid image produces terminal decode error instead of permanent loading");
    p.Key(VK_DELETE);
    const auto navigation_start=std::chrono::steady_clock::now();for(int i=0;i<1000;++i)p.Key(i%2?VK_UP:VK_DOWN);const auto navigation_us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-navigation_start).count();std::cout<<"KEYBOARD dispatch1000_us="<<navigation_us<<" (no paint or OS paste)\n";
    p.Fold();expect(p.disk->WaitIdle(),"fold cancels thumbnail work");p.DrainStore();expect(p.images.empty()&&p.file_bitmaps.empty()&&p.disk->Stats().result_bytes==0,"fold releases image caches and drops late decoded results");
    wchar_t cycles_text[16]{};const DWORD cycle_length=GetEnvironmentVariableW(L"LUMASHOT_CLIPBOARD_MEMORY_CYCLES",cycles_text,16);const int cycles=cycle_length&&cycle_length<16?std::clamp(static_cast<int>(wcstol(cycles_text,nullptr,10)),1,1000):10;
    for(int cycle=0;cycle<cycles;++cycle){p.expanded=true;p.Place();p.Render();expect(p.disk->WaitIdle(),"open cycle thumbnail worker settles");p.DrainStore();p.Render();p.Fold();expect(p.disk->WaitIdle(),"close cycle worker settles");p.DrainStore();MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}expect(p.images.empty()&&p.file_bitmaps.empty()&&p.disk->Stats().result_bytes==0,"close cycle holds no thumbnail payloads");
        if(cycle==9||cycle==49||cycle==99||cycle==149||cycle==199||cycle==249||cycle==cycles-1){PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));std::cout<<"PANEL_MEMORY close_cycle="<<cycle+1<<" private="<<memory.PrivateUsage<<" working_set="<<memory.WorkingSetSize<<" gdi="<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<"\n";}}
    const auto session=p.disk->Directory();p.Enable(false,false);expect(!p.disk&&p.history.entries.empty()&&!std::filesystem::exists(session),"disable releases worker metadata and encrypted session files");
    return failures?1:0;
}
};
}
int main(int argc,char** argv){CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);const int result=argc>1&&std::string(argv[1])=="--appearance"?lumashot::ClipboardPanelTest::Appearance():argc>1&&std::string(argv[1])=="--features"?lumashot::ClipboardPanelTest::Features():argc>1&&std::string(argv[1])=="--groups"?lumashot::ClipboardPanelTest::Groups():argc>1&&std::string(argv[1])=="--pin-only"?lumashot::ClipboardPanelTest::PinOnly():lumashot::ClipboardPanelTest::Run();CoUninitialize();return result;}

