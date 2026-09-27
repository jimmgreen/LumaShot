// Hidden native windows and synthetic records only; no clipboard I/O or personal INI.
#include "../src/clipboard/panel.cpp"
#include "export/png.h"
#include <array>
#include <iostream>

namespace lumashot {
struct ClipboardPanelTest {
    static bool Overlap(D2D1_RECT_F a,D2D1_RECT_F b) {
        return a.left<b.right&&b.left<a.right&&a.top<b.bottom&&b.top<a.bottom;
    }
    static void Snapshot(ClipboardPanel::Impl& p,const std::filesystem::path& path) {
        auto frame=MakeFrame({0,0,p.width,p.height});
        const UINT background=p.dark?0x151b23:0xf5f8fc;
        for(size_t i=0;i<frame.pixels.size();++i) {
            const auto pixel=p.surface->Pixels()[i],inverse=255-(pixel>>24);
            UINT opaque=0xff000000;
            for(UINT shift:{0u,8u,16u})opaque|=std::min(255u,((pixel>>shift)&255)+(((background>>shift)&255)*inverse+127)/255)<<shift;
            frame.pixels[i]=opaque;
        }
        SavePng(frame,path);
    }
    static int Restore(const std::filesystem::path& file,int expected_w,int expected_h) {
        ClipboardPanel panel(nullptr,[]{});auto& p=*panel.impl_;
        p.test_mode=true;p.layout_path=file;
        if(!p.Enable(true,false))return 2;
        const bool ok=std::abs(p.W-expected_w)<.1f&&std::abs(p.H-expected_h)<.1f&&!p.disk&&!p.hotkey;
        p.Enable(false,false);
        return ok?0:3;
    }
    static int Run(bool policy_only) {
        int checks=0,failures=0;
        const auto expect=[&](bool ok,const char* message) {
            ++checks;
            if(!ok){++failures;std::cout<<"FAIL "<<message<<'\n';}
        };
        const auto directory=std::filesystem::current_path()/(L"fixture-"+std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(directory);
        const auto pure=directory/L"roundtrip.ini";
        for(LONG width:{280,300,320,400,560}) {
            const bool wrote=clipboard::SavePosition(pure,{-1200,80},SIZE{width,620});
            if(!wrote)std::cout<<"SAVE width="<<width<<" error="<<GetLastError()<<'\n';
            expect(wrote,"write isolated DIP size");
            const auto loaded=clipboard::LoadPanelSize(pure);
            expect(loaded&&loaded->cx==width&&loaded->cy==620,"narrow and existing widths survive persistence validation");
            const bool moved_ok=clipboard::SavePosition(pure,{-1150,90});
            if(!moved_ok)std::cout<<"MOVE width="<<width<<" error="<<GetLastError()<<'\n';
            expect(moved_ok,"move collapsed tab without replacing dimensions");
            const auto moved=clipboard::LoadPanelSize(pure);
            expect(moved&&moved->cx==width&&moved->cy==620,"collapsed move preserves narrow width");
        }
        for(const wchar_t* value:{L"-1",L"0",L"279",L"99999"}) {
            WritePrivateProfileStringW(L"Panel",L"Width",value,pure.c_str());
            expect(!clipboard::LoadPanelSize(pure),"invalid saved widths are rejected");
        }
        ClipboardPanel panel(nullptr,[]{});auto& p=*panel.impl_;
        p.test_mode=true;
        if(!p.Enable(true,false)){std::cout<<"FAIL create hidden panel\n";return 1;}
        expect(p.layout_path.empty()&&!p.disk&&!p.hotkey,"test mode does not resolve personal preferences or history");
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(p.window,MONITOR_DEFAULTTONEAREST),&monitor);
        for(float scale:{1.f,1.25f,1.5f,2.f}) {
            p.expanded=true;p.scale=scale;
            MINMAXINFO limits{};
            SendMessageW(p.window,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&limits));
            expect(limits.ptMinTrackSize.x==std::min(monitor.rcWork.right-monitor.rcWork.left-8,static_cast<LONG>(280*scale)),
                   "native minimum width is 280 DIP at each DPI");
        }
        if(policy_only){p.Enable(false,false);std::cout<<"WIDTH_POLICY checks="<<checks<<" failures="<<failures<<std::endl;return failures?1:0;}
        const auto layout=directory/L"native-layout.ini";
        p.layout_path=layout;
        const auto resize=[&](int w,int h,float scale) {
            p.expanded=true;p.scale=scale;
            SendMessageW(p.window,WM_ENTERSIZEMOVE,0,0);
            SetWindowPos(p.window,nullptr,monitor.rcWork.left+10,monitor.rcWork.top+10,
                static_cast<int>(std::lround(w*scale)),static_cast<int>(std::lround(h*scale)),SWP_NOZORDER|SWP_NOACTIVATE);
            SendMessageW(p.window,WM_EXITSIZEMOVE,0,0);
        };
        resize(320,620,1.5f);
        auto saved=clipboard::LoadPanelSize(layout);
        expect(saved&&saved->cx==320&&saved->cy==620,"native sizing completion persists logical DIP dimensions");
        p.Fold();
        expect(p.W==320&&p.H==620,"folding leaves expanded dimensions intact");
        p.anchor={monitor.rcWork.right-12,monitor.rcWork.top+20};
        p.drag_pending=p.dragged=true;p.EndDrag();
        saved=clipboard::LoadPanelSize(layout);const auto position=clipboard::LoadPosition(layout);
        expect(saved&&saved->cx==320&&saved->cy==620&&position&&position->x==p.anchor.x&&position->y==p.anchor.y,
               "collapsed drag saves anchor without losing user width");
        p.Enable(false,false);p.W=400;p.H=744;p.placed=false;
        expect(p.Enable(true,false)&&p.W==320&&p.H==620,"disabling and reopening restores the saved size");
        p.Enable(false,false);
        expect(Restore(layout,320,620)==0,"a new panel instance restores narrow dimensions");
        wchar_t executable[MAX_PATH]{};GetModuleFileNameW(nullptr,executable,MAX_PATH);
        std::wstring command=L"\""+std::wstring(executable)+L"\" --restore \""+layout.wstring()+L"\" 320 620";
        STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION child{};
        const bool launched=CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)!=FALSE;
        expect(launched,"launch isolated process-restart fixture");
        if(launched) {
            const auto waited=WaitForSingleObject(child.hProcess,30000);
            if(waited!=WAIT_OBJECT_0){TerminateProcess(child.hProcess,5);WaitForSingleObject(child.hProcess,5000);}
            DWORD code{};GetExitCodeProcess(child.hProcess,&code);
            expect(waited==WAIT_OBJECT_0&&code==0,"a separate process restores persisted panel dimensions");
            CloseHandle(child.hThread);CloseHandle(child.hProcess);
        }
        expect(p.Enable(true,false),"reopen isolated rendering fixture");
        const auto text=[&](const std::wstring& value) {
            clipboard::Entry e;e.kind=clipboard::Kind::Text;e.text=value;GetLocalTime(&e.time);
            const auto* b=reinterpret_cast<const unsigned char*>(e.text.c_str());
            e.formats.push_back({CF_UNICODETEXT,{b,b+(e.text.size()+1)*sizeof(wchar_t)}});p.history.Add(std::move(e));
        };
        text(L"Synthetic text: 用户调整的宽度应保持，长文本需要省略而不是挤住按钮。");
        text(L"https://example.invalid/synthetic-width-fixture");
        {
            clipboard::Entry e;e.kind=clipboard::Kind::Files;e.text=L"C:\\Synthetic\\narrow-layout-example.txt";GetLocalTime(&e.time);
            e.formats={{CF_HDROP,{1,2,3}}};p.history.Add(std::move(e));
        }
        {
            clipboard::Entry e;e.kind=clipboard::Kind::Image;e.text=L"合成图片";GetLocalTime(&e.time);
            BITMAPINFOHEADER h{};h.biSize=sizeof(h);h.biWidth=120;h.biHeight=-80;h.biPlanes=1;h.biBitCount=32;h.biCompression=BI_RGB;
            std::vector<unsigned char> dib(sizeof(h)+120*80*4);std::memcpy(dib.data(),&h,sizeof(h));
            for(int y=0;y<80;++y)for(int x=0;x<120;++x){const uint32_t color=y<40?0xff83b9e4u:0xff326a91u;std::memcpy(dib.data()+sizeof(h)+(y*120+x)*4,&color,4);}
            e.formats={{CF_DIB,std::move(dib)}};p.history.Add(std::move(e));
        }
        text(L"const sample = 'synthetic code';\nreturn sample; // narrow panel");
        for(float scale:{1.f,1.25f,1.5f,2.f})for(int width:{280,320,400,560})for(bool dark:{false,true}) {
            p.dark=dark;p.tab=0;SetWindowTextW(p.search,L"");p.status.clear();
            resize(width,620,scale);p.Filter();p.LayoutSearch();p.Render();
            expect(p.file_icons->WaitIdle(10000),"synthetic file icons settle");p.Render();
            expect(std::abs(p.W-width)<1&&std::abs(p.scale-scale)<.01f,"resize changes width without shrinking fonts");
            expect(p.text_glyphs>0,"narrow rendering uses the actual LumaText path");
            const auto dimensions=clipboard::LoadPanelSize(layout);
            expect(dimensions&&dimensions->cx==width&&dimensions->cy==620,"resizing round-trips across themes and DPI");
            std::vector<D2D1_RECT_F> controls;
            std::array<D2D1_RECT_F,5> tabs{};D2D1_RECT_F sort{};int categories=0;
            for(const auto& hit:p.hits) {
                if(hit.action>=10&&hit.action<=24) {
                    expect(hit.rect.left>=0&&hit.rect.right<=p.W&&hit.rect.top>=0&&hit.rect.bottom<=p.H,
                           "all header/category/footer controls stay within the panel");
                    expect(hit.rect.right>hit.rect.left&&hit.rect.bottom>hit.rect.top,"control rectangles remain positive");
                    controls.push_back(hit.rect);
                }
                if(hit.action>=20&&hit.action<=24){tabs[static_cast<size_t>(hit.action-20)]=hit.rect;++categories;}
                if(hit.action==13)sort=hit.rect;
                if(hit.action>=31&&hit.action<=34) {
                    const auto card=std::find_if(p.hits.begin(),p.hits.end(),[&](const auto& row){return row.action==30&&row.id==hit.id;});
                    expect(card!=p.hits.end()&&hit.rect.left>=card->rect.left+4&&hit.rect.right<=card->rect.right-4&&hit.rect.bottom<=card->rect.bottom-4,
                           "card action click targets remain inside each card");
                    expect(hit.rect.right-hit.rect.left==32&&hit.rect.bottom-hit.rect.top==32,"card actions keep full-size click targets");
                }
            }
            for(size_t i=0;i<controls.size();++i)for(size_t j=i+1;j<controls.size();++j)
                expect(!Overlap(controls[i],controls[j]),"independent controls do not overlap at narrow widths");
            expect(categories==5&&tabs[4].right<=sort.left-4,"all five categories fit before the sort button");
            RECT search{};GetWindowRect(p.search,&search);MapWindowPoints(nullptr,p.window,reinterpret_cast<POINT*>(&search),2);
            expect(search.right>search.left&&search.left>=0&&search.right<=static_cast<LONG>(std::lround(p.W*scale)),"native search remains usable inside resized panel");
            RECT bounds{};GetWindowRect(p.window,&bounds);
            expect(p.ResizeHit({bounds.left+1,(bounds.top+bounds.bottom)/2})==HTLEFT&&p.ResizeHit({bounds.right-1,(bounds.top+bounds.bottom)/2})==HTRIGHT,
                   "both edges keep native resizing cursors");
            if(width==280||width==400&&scale==1.f)
                Snapshot(p,directory/(L"panel-"+std::to_wstring(width)+L"-"+std::to_wstring(static_cast<int>(scale*100))+(dark?L"-dark.png":L"-light.png")));
            p.Fold();expect(p.W==width&&p.H==620,"folding preserves user-selected size at every DPI");
        }
        p.Enable(false,false);
        std::wcout<<L"Width fixtures: "<<directory.wstring()<<std::endl;
        std::cout<<"CLIPBOARD_WIDTH checks="<<checks<<" failures="<<failures<<std::endl;
        return failures?1:0;
    }
};
}
int wmain(int argc,wchar_t** argv) {
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int result=1;
    try {
        if(argc==5&&std::wstring(argv[1])==L"--restore")result=lumashot::ClipboardPanelTest::Restore(argv[2],_wtoi(argv[3]),_wtoi(argv[4]));
        else result=lumashot::ClipboardPanelTest::Run(argc==2&&std::wstring(argv[1])==L"--policy");
    } catch(const std::exception& error) {std::cerr<<error.what()<<std::endl;}
    CoUninitialize();return result;
}
