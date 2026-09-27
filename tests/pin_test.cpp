// Include the implementation to exercise the real private window state without
// adding test commands or diagnostic interfaces to the shipping application.
#include <windows.h>
#include <functional>
#include "ui/themed_message.h"
namespace {
std::function<bool(HWND,bool,const std::wstring&,const std::wstring&)> message_override;
}
namespace lumashot::ui {
bool TestThemedMessage(HWND owner,bool dark,const std::wstring& title,const std::wstring& body){
    return message_override?message_override(owner,dark,title,body):ShowThemedMessage(owner,dark,title,body);
}
}
// Test-only interception: production PinManager::Saved still runs unchanged.
// A deterministic callback injects the same reentrant call that a modal loop can
// dispatch, without relying on platform-specific MessageBox automation.
#define ShowThemedMessage TestThemedMessage
#include "../src/pin/pin.cpp"
#undef ShowThemedMessage
#include <wincodec.h>
#include <tlhelp32.h>
#include <iostream>
namespace lumashot {
struct PinTest {
    static void Pump(int milliseconds) {
        const auto end=GetTickCount64()+static_cast<ULONGLONG>(milliseconds);MSG msg{};
        while(GetTickCount64()<end){while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(5);}
    }
    static LRESULT CALLBACK Host(HWND w,UINT m,WPARAM a,LPARAM b){return DefWindowProcW(w,m,a,b);}
    static size_t Workers(){
        size_t count=0;ocr::Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));PROCESSENTRY32W entry{sizeof(entry)};
        if(Process32FirstW(snapshot.Get(),&entry))do{if(entry.th32ParentProcessID==GetCurrentProcessId()&&std::wstring(entry.szExeFile)==L"lumashot_ocr_worker.exe")++count;}while(Process32NextW(snapshot.Get(),&entry));return count;
    }
    static Frame Preview(PinManager& manager,PinManager::Pin& p) {
        Frame frame=MakeFrame({0,0,p.CanvasWidth(),p.CanvasHeight()});
        ComPtr<IWICImagingFactory> wic;CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(wic.GetAddressOf()));
        ComPtr<IWICBitmap> bitmap;wic->CreateBitmap(static_cast<UINT>(frame.Width()),static_cast<UINT>(frame.Height()),GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,bitmap.GetAddressOf());
        ComPtr<ID2D1Factory> factory;D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf());ComPtr<ID2D1RenderTarget> target;
        auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        CheckWin32(SUCCEEDED(factory->CreateWicBitmapRenderTarget(bitmap.Get(),props,target.GetAddressOf())),"Create pin preview");manager.Draw(p,target.Get());
        bitmap->CopyPixels(nullptr,static_cast<UINT>(frame.Width()*4),static_cast<UINT>(frame.pixels.size()*4),reinterpret_cast<BYTE*>(frame.pixels.data()));return frame;
    }
    static int ZoomUi(){
        int failures=0;
        PinManager manager(nullptr);
        for(bool dark:{false,true})for(float dpi:{96.f,144.f,192.f}){
            manager.dark_theme=[dark]{return dark;};
            PinManager::Pin pin;pin.manager=&manager;pin.dpi=dpi;pin.zoom=1.25f;
            pin.image=std::make_shared<Frame>(MakeFrame({0,0,280,100},dark?0xff18202c:0xfff4f7fb));
            pin.paper=MakePaperLayout(350,125,dpi,pin.style);
            pin.zoom_badge_until=GetTickCount64()+60000;
            const auto frame=Preview(manager,pin);
            const float scale=dpi/96,w=64*scale,h=26*scale;
            const float x=pin.paper.shadow+(pin.paper.width-w)/2,y=pin.paper.shadow+6*scale;
            size_t bright=0;
            for(int yy=int(y+4*scale);yy<int(y+h-4*scale);++yy)
                for(int xx=int(x+8*scale);xx<int(x+w-8*scale);++xx){
                    const auto pixel=frame.pixels[static_cast<size_t>(yy)*frame.Width()+xx];
                    if(((pixel>>16)&255)>200&&((pixel>>8)&255)>200&&(pixel&255)>200)++bright;
                }
            const bool visible=bright>static_cast<size_t>(12*scale*scale);
            std::cout<<(visible?"PASS ":"FAIL ")<<"zoom badge text on "<<(dark?"dark":"light")<<" synthetic image at "<<int(dpi)<<" DPI ("<<bright<<" bright pixels)"<<std::endl;
            failures+=!visible;
            SavePng(frame,std::wstring(L"pin-zoom-ui-")+(dark?L"dark-":L"light-")+std::to_wstring(int(dpi))+L".png");
        }
        return failures?1:0;
    }
    static int SaveReentry(){
        int failures=0;auto expect=[&](bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<std::endl;if(!ok)++failures;};
        HWND host=CreateWindowExW(0,L"STATIC",L"Synthetic save completion host",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!host)throw std::runtime_error("Create synthetic save host");
        {
            PinManager manager(host);
            for(uint64_t id:{1ull,2ull}){
                auto pin=std::make_unique<PinManager::Pin>();pin->manager=&manager;pin->id=id;pin->saving=true;
                pin->window=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Synthetic save owner",WS_POPUP,0,0,80,80,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
                if(!pin->window)throw std::runtime_error("Create synthetic save owner");manager.pins_.emplace(id,std::move(pin));
            }
            std::promise<PinManager::SaveResult> slow,failed;
            manager.saves_.push_back(slow.get_future());manager.saves_.push_back(failed.get_future());
            PinManager::SaveResult error;error.id=2;error.error=L"Synthetic injected failure";failed.set_value(std::move(error));
            int prompts=0;
            struct Restore{~Restore(){message_override={};}} restore;
            manager.dark_theme=[] {return true;};
            message_override=[&](HWND owner,bool dark,const std::wstring& title,const std::wstring& body){
                expect(owner==manager.pins_.at(2)->window&&dark&&title==L"导出失败"&&body==L"Synthetic injected failure","themed failure notification preserves owner, theme and error content");
                ++prompts;PinManager::SaveResult value;value.id=1;slow.set_value(std::move(value));
                manager.Saved();expect(manager.saves_.size()==1,"reentrant completion stays queued while notification is active");
                std::promise<PinManager::SaveResult> arriving;manager.saves_.push_back(arriving.get_future());
                PinManager::SaveResult closed;closed.id=999;arriving.set_value(std::move(closed));
                manager.Saved();expect(manager.saves_.size()==2,"newly submitted job cannot invalidate outer completion traversal");
                return false;
            };
            manager.Saved();expect(prompts==1,"only the failed result produces a notification");
            manager.Saved();expect(manager.saves_.empty(),"deferred and closed-pin completions drain after notification");
            expect(!manager.pins_.at(1)->saving&&!manager.pins_.at(2)->saving,"both busy flags clear exactly once");
            std::promise<PinManager::SaveResult> next;manager.saves_.push_back(next.get_future());
            PinManager::SaveResult value;value.id=1;next.set_value(std::move(value));manager.Saved();
            expect(manager.saves_.empty(),"guard resets and subsequent completions remain safe");
        }
        DestroyWindow(host);return failures?1:0;
    }
    static int Run() {
        int failures=0;auto expect=[&](bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<std::endl;if(!ok)++failures;};
        WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=Host;wc.lpszClassName=L"LumaShot.PinTest.Host";RegisterClassW(&wc);
        HWND host=CreateWindowW(wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);
        {
            PinManager manager(host);expect(Workers()==0,"no OCR process before first pin");
            auto base=MakeFrame({0,0,680,220},0xfffafafa);Document doc;
            Mark mark;mark.tool=Tool::Text;mark.a={24,24};mark.b={650,62};mark.font_size=24;mark.color=0xff202733;mark.text=L"LumaShot 文字选择复制";doc.Add(mark);
            mark.a.y=84;mark.b.y=122;mark.text=L"Hello Windows 2026";doc.Add(mark);
            Renderer renderer;auto image=renderer.Flatten(base,doc,base.bounds);
            const auto original=image.pixels;manager.Create(image,image,{100,100},std::nullopt,{},false);auto& p=*manager.pins_.begin()->second;
            Pump(150);
            expect(IsWindow(p.window)&&!p.recognizing&&!p.ocr_enabled&&p.version==0&&p.status.empty()&&Workers()==0,"ordinary pin never starts OCR or displays pending status");
            manager.CompleteAnnotation(p.id,image,{},std::nullopt,false);
            Pump(100);expect(!p.recognizing&&!p.ocr_enabled&&p.text.lines.empty()&&Workers()==0,"editing ordinary pin does not start OCR");
            SetTimer(nullptr,0,30,[](HWND,UINT,UINT_PTR timer,DWORD){
                const HWND menu=FindWindowW(L"LumaShot.PinMenu",nullptr);if(!menu)return;KillTimer(nullptr,timer);
                PostMessageW(menu,WM_KEYDOWN,VK_END,0);PostMessageW(menu,WM_KEYDOWN,VK_UP,0);PostMessageW(menu,WM_KEYDOWN,VK_RETURN,0);
            });
            manager.Menu(p,{150,150});
            expect(p.recognizing&&p.ocr_enabled,"right-click recognize starts OCR explicitly");
            RECT initial{};GetClientRect(p.window,&initial);expect(initial.bottom==image.Height()+2*p.Inset()+2&&initial.right==image.Width()+2*p.Inset()+2,"paper padding surrounds full image without title bar");
            POINT content{p.Inset(),p.Inset()};ClientToScreen(p.window,&content);expect(content.x==100&&content.y==100,"paper frame preserves original screenshot position");
            const auto plain=Preview(manager,p);expect(Crop(plain,{p.Inset(),p.Inset(),p.Inset()+image.Width(),p.Inset()+image.Height()}).pixels==original,"paper and fold leave every image pixel intact");
            bool partial=false;for(auto pixel:plain.pixels){auto alpha=pixel>>24;partial|=alpha>0&&alpha<255;}
            expect(partial&&(GetWindowLongPtrW(p.window,GWL_EXSTYLE)&WS_EX_LAYERED),"paper silhouette preserves fractional alpha instead of binary region clipping");
            SendMessageW(p.window,WM_KEYDOWN,'L',0);expect(p.locked,"L locks pin");
            SendMessageW(p.window,WM_KEYDOWN,'L',1LL<<30);expect(p.locked,"held L does not repeatedly toggle lock");
            SendMessageW(p.window,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),MAKELPARAM(400,200));expect(p.zoom_goal==1,"locked pin blocks wheel zoom");
            SendMessageW(p.window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(p.Inset()+600,p.Inset()+180));expect(!p.moving,"locked pin blocks drag");
            SendMessageW(p.window,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(p.Inset()+600,p.Inset()+180));expect(IsWindow(p.window)!=FALSE,"locked pin ignores blank double click");
            SendMessageW(p.window,WM_KEYDOWN,'L',0);expect(!p.locked,"L unlocks pin");
            int annotate_calls=0;manager.annotate=[&](uint64_t id,std::shared_ptr<const Frame> input,Document,RECT bounds){++annotate_calls;return id==p.id&&input&&input->pixels==original&&bounds.right-bounds.left==image.Width();};
            SendMessageW(p.window,WM_KEYDOWN,VK_SPACE,0);SendMessageW(p.window,WM_KEYDOWN,VK_SPACE,1LL<<30);
            expect(p.editing&&annotate_calls==1,"Space opens annotation directly, once per key press");
            manager.CompleteAnnotation(p.id,std::nullopt);expect(!p.editing&&p.image->pixels==original,"cancel annotation preserves pin pixels");
            POINT anchor{p.Inset()+240,p.Inset()+100};ClientToScreen(p.window,&anchor);const auto version_before_zoom=p.version;
            SendMessageW(p.window,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),MAKELPARAM(anchor.x,anchor.y));
            expect(p.zoom==1&&p.zoom_goal>1,"wheel starts a finite smooth transition");
            bool stable=true;float previous=1;
            for(int step=0;step<12;++step){Pump(16);RECT r{};GetWindowRect(p.window,&r);auto q=p.WindowPoint({240,100});
                stable&=std::abs(r.left+q.x-anchor.x)<.01f&&std::abs(r.top+q.y-anchor.y)<.01f&&p.zoom>=previous;previous=p.zoom;
            }
            expect(stable,"every animation frame keeps subpixel cursor anchor fixed with monotonic scale");Pump(80);
            POINT local=anchor;ScreenToClient(p.window,&local);const auto mapped_zoom=p.ImagePoint(MAKELPARAM(local.x,local.y));
            expect(std::abs(p.zoom-1.12f)<.001f&&std::abs(mapped_zoom.x-240)<1&&std::abs(mapped_zoom.y-100)<1,"zoom settles with cursor image point anchored");
            expect(p.image->pixels==original&&p.version==version_before_zoom&&p.zoom_badge_until>GetTickCount64(),"zoom retains source pixels and OCR and displays percentage");
            manager.Paint(p);auto surface_frame=MakeFrame({0,0,p.CanvasWidth(),p.CanvasHeight()},0);
            std::copy_n(p.surface->Pixels(),surface_frame.pixels.size(),surface_frame.pixels.begin());
            bool surface_alpha=false;for(auto pixel:surface_frame.pixels){auto alpha=pixel>>24;surface_alpha|=alpha>0&&alpha<255;}
            expect(surface_alpha&&surface_frame.pixels.front()==0,"actual layered window surface keeps transparent corners and antialiased edges");
            SavePng(surface_frame,L"pin-zoom-preview.png");
            SendMessageW(p.window,WM_MOUSEWHEEL,MAKEWPARAM(0,-WHEEL_DELTA),MAKELPARAM(anchor.x,anchor.y));Pump(250);
            expect(std::abs(p.zoom-1)<.001f,"reverse wheel returns to original scale");
            Pump(950);expect(p.zoom_badge_until==0,"percentage hides when interaction settles");
            const auto builds=p.shadow_builds;
            RECT before_move{};GetWindowRect(p.window,&before_move);p.moving=true;
            bool synchronized=true;
            for(int step=0;step<120;++step){
                SetWindowPos(p.window,nullptr,before_move.left+step*2,before_move.top+step%20,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOZORDER);
                RECT r{};GetWindowRect(p.window,&r);synchronized&=r.left==before_move.left+step*2&&r.top==before_move.top+step%20;
            }
            SetWindowPos(p.window,nullptr,before_move.left,before_move.top,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOZORDER);p.moving=false;
            expect(synchronized&&p.shadow_builds==builds,"single composite window moves paper and shadow together without rebuilding");
            expect(!p.Blank({35,35})&&p.Blank({600,180}),"pending OCR protects visible text but allows uniform blank close");
            const auto deadline=GetTickCount64()+15000;
            while(p.text.lines.empty()&&GetTickCount64()<deadline){Pump(20);manager.Ready();}
            expect(!p.text.lines.empty(),"real worker IPC recognizes pinned image");
            expect(Workers()==1,"one shared OCR worker");
            if(!p.text.lines.empty()) {
                const auto& gs=p.text.lines.front().glyphs;
                const auto a=gs.front().box,b=gs.back().box;
                const auto raw_lp=[](float x,float y){return MAKELPARAM(static_cast<short>(x),static_cast<short>(y));};
                const auto lp=[&](float x,float y){const auto point=p.WindowPoint({x,y});return raw_lp(point.x,point.y);};
                SendMessageW(p.window,WM_LBUTTONDOWN,MK_LBUTTON,lp(std::ceil(a.left)+1,(a.top+a.bottom)/2));
                SendMessageW(p.window,WM_MOUSEMOVE,MK_LBUTTON,lp(b.right-0.1f,(b.top+b.bottom)/2));SendMessageW(p.window,WM_LBUTTONUP,0,lp(b.right-0.1f,(b.top+b.bottom)/2));
                expect(!p.selection.Empty()&&!ocr::Selected(p.text,p.selection).empty(),"real mouse messages select text");
                expect(p.tools&&IsWindowVisible(p.tools),"selection opens independent toolbar");
                const auto all=p.selection;auto handles=p.Handles();
                SendMessageW(p.window,WM_LBUTTONDOWN,MK_LBUTTON,lp(handles.end.x,handles.end.y));
                expect(p.selecting&&!p.moving&&!IsWindowVisible(p.tools),"end handle starts selection adjustment without moving image");
                const auto middle=gs[gs.size()/2].box;
                SendMessageW(p.window,WM_MOUSEMOVE,MK_LBUTTON,lp(middle.right,handles.end.y));SendMessageW(p.window,WM_LBUTTONUP,0,lp(middle.right,handles.end.y));
                expect(p.selection.caret<all.caret&&IsWindowVisible(p.tools),"handle contracts text selection and restores toolbar");
                p.selection=all;manager.UpdateTools(p);
                manager.Copy(p);for(int wait=0;wait<100&&p.saving;++wait){Pump(10);manager.Saved();}
                bool copied_image=false;if(OpenClipboard(p.window)){const auto memory=GetClipboardData(CF_DIB);const auto* header=static_cast<const BITMAPINFOHEADER*>(GlobalLock(memory));if(header){copied_image=header->biWidth==p.image->Width()&&header->biHeight==p.image->Height();GlobalUnlock(memory);}CloseClipboard();}
                expect(!p.saving&&copied_image&&p.image->pixels==original,"background pin copy publishes full image without mutating source");
                manager.ToolCommand(p,0);expect(p.copied,"copy shows success feedback");SendMessageW(p.tools,WM_TIMER,1,0);expect(!p.copied,"copy feedback resets without a persistent timer");
                for(int command=1;command<=4;++command)manager.ToolCommand(p,command);
                expect(p.marks.size()==4&&p.undo.size()==4,"all four selection marking tools apply");
                expect(FlattenTextMarks(*p.image,p.marks).pixels!=original&&p.image->pixels==original,"mark export changes pixels without modifying OCR source");
                manager.ToolCommand(p,4);expect(p.marks.size()==3,"same marking tool toggles off for same selection");
                p.marks.clear();p.undo.clear();
                RECT tools_rect{};GetClientRect(p.tools,&tools_rect);const auto tool_point=raw_lp(float(tools_rect.right)/2,float(tools_rect.bottom)/2);
                SendMessageW(p.tools,WM_LBUTTONDOWN,MK_LBUTTON,tool_point);SendMessageW(p.tools,WM_LBUTTONUP,0,tool_point);
                expect(p.marks.size()==1&&p.marks.front().kind==TextDecoration::Wave&&!p.selection.Empty()&&IsWindowVisible(p.tools),"toolbar click applies wave and preserves selection");
                p.marks.clear();p.undo.clear();
                expect(FlattenTextMarks(*p.image,p.marks).pixels==original,"image export excludes selection handles and toolbar");
                SavePng(Preview(manager,p),L"build/pin-selection-preview.png");
                SendMessageW(p.window,WM_KEYDOWN,VK_ESCAPE,0);expect(p.selection.Empty()&&!IsWindowVisible(p.tools),"Escape clears selection and hides toolbar while keeping pin");
                p.clicks=0;const auto point=lp((a.left+a.right)/2,(a.top+a.bottom)/2);
                for(int click=0;click<3;++click){SendMessageW(p.window,click==1?WM_LBUTTONDBLCLK:WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(p.window,WM_LBUTTONUP,0,point);}
                expect(p.selection.anchor.glyph==0&&p.selection.caret.glyph==gs.size(),"triple click selects entire line");
                expect(IsWindow(p.window)!=FALSE,"double and triple click on text never closes pin");
                expect(p.image->pixels==original,"text highlight never modifies image pixels");
                RECT r{-100,-100,900,900};const auto version=p.version;
                SendMessageW(p.window,WM_DPICHANGED,MAKELONG(144,144),reinterpret_cast<LPARAM>(&r));
                RECT client{};GetClientRect(p.window,&client);expect(client.right-2*p.Inset()-2==image.Width()&&client.bottom-2*p.Inset()-2==image.Height()&&p.version==version,"DPI change preserves physical image width without OCR rerun");
                const auto client_point=p.WindowPoint({a.left,a.top});const auto mapped=p.ImagePoint(raw_lp(client_point.x,client_point.y));expect(std::abs(mapped.x-a.left)<1&&std::abs(mapped.y-a.top)<1,"DPI-scaled paper inset maps pointer back to OCR pixels");
                SavePng(Preview(manager,p),L"build/pin-150dpi-preview.png");
            }
            p.text.table=ocr::Table{{0,0,300,80},2,3,{L"项目",L"",L"数量",L"笔记本",L"单价\n说明",L"2"}};
            p.status.clear();const auto table_version=p.version;
            SetTimer(nullptr,0,30,[](HWND,UINT,UINT_PTR timer,DWORD){
                const HWND menu=FindWindowW(L"LumaShot.PinMenu",nullptr);if(!menu)return;KillTimer(nullptr,timer);
                PinMenuModel model;model.table=true;const float scale=float(GetDpiForWindow(menu))/96;
                const auto point=MAKELPARAM(int((PinMenuModel::Shadow+70)*scale),int((PinMenuModel::Shadow+model.RowTop(4)+21)*scale));
                PostMessageW(menu,WM_LBUTTONDOWN,0,point);PostMessageW(menu,WM_LBUTTONUP,0,point);
            });
            manager.Menu(p,{150,150});
            bool pasted=false;
            if(GetClipboardOwner()==p.window){
                HWND sink=CreateWindowExW(0,L"EDIT",L"",WS_POPUP|ES_MULTILINE,0,0,300,100,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
                SendMessageW(sink,WM_PASTE,0,0);wchar_t value[256]{};GetWindowTextW(sink,value,256);
                pasted=std::wstring(value)==L"项目\t\t数量\r\n笔记本\t单价 说明\t2";DestroyWindow(sink);
            }
            expect(pasted&&p.version==table_version,"copy table menu pastes TSV with empty cells and flattened cell newlines without rerunning OCR");
            manager.Recognize(p);const auto current=p.version;manager.Recognize(p);
            expect(p.version==current+1,"retry versions supersede previous recognition");
            manager.Create(image,image,{150,160});auto* second=manager.pins_.rbegin()->second.get();expect(second->recognizing&&second->ocr_enabled,"default pin starts OCR automatically");
            Document editable;Mark rectangle;rectangle.a={30,30};rectangle.b={180,120};rectangle.rotation=15;rectangle.corner_radii=std::array<float,4>{10,15,20,25};editable.Add(rectangle);
            manager.CompleteAnnotation(second->id,renderer.Flatten(image,editable,image.bounds),editable,image);
            bool editable_reopened=false;manager.annotate=[&](uint64_t id,std::shared_ptr<const Frame> input,Document document,RECT){editable_reopened=id==second->id&&input&&input->pixels==original&&document.marks==editable.marks;return true;};
            SendMessageW(second->window,WM_KEYDOWN,VK_SPACE,0);expect(editable_reopened,"reopening annotation retains source and editable rotated geometry");manager.CompleteAnnotation(second->id,std::nullopt);
            const uint64_t closed=second->id;SendMessageW(second->window,WM_CLOSE,0,0);manager.Ready();
            expect(!manager.pins_.contains(closed),"closing queued pin discards it immediately");
            Pump(2500);manager.Ready();expect(p.version==current+1&&!p.text.lines.empty(),"latest retry completes after cancellation");
            expect(Workers()==1,"multiple pins never start parallel OCR workers");
            const HWND closing=p.window;
            SendMessageW(closing,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(600,180));SendMessageW(closing,WM_LBUTTONUP,0,MAKELPARAM(600,180));
            expect(IsWindow(closing)!=FALSE,"single blank click keeps pin open");
            SendMessageW(closing,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(600,180));
            expect(!IsWindow(closing),"double click blank closes pin");manager.Ready();
            
            expect(manager.pins_.empty(),"closed pin releases image and selection state");
            std::cout<<"waiting for 30 second worker idle release"<<std::endl;
            Pump(31500);expect(Workers()==0,"idle OCR process exits and releases its memory");
        }
        DestroyWindow(host);return failures?1:0;
    }
};
}
int main(int argc,char** argv){SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int result=1;try{const std::string mode=argc>1?argv[1]:"";result=mode=="--zoom-ui"?lumashot::PinTest::ZoomUi():mode=="--save-reentry"?lumashot::PinTest::SaveReentry():lumashot::PinTest::Run();}catch(const std::exception& e){std::cout<<"FAIL "<<e.what()<<std::endl;}CoUninitialize();return result;}



