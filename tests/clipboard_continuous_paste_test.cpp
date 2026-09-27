// Production target selection, publication and paste transaction on a private
// window station. Only OS input injection is replaced with native WM_PASTE to
// the owned edit: SendInput cannot target a non-input desktop.
#include "clipboard_preview_probe.h"
#include <iostream>
// A non-input desktop has no global foreground window. Use its real thread
// activation state for the same production foreground checks.
static HWND external_foreground{};
static HWND SyntheticForeground(){return external_foreground?external_foreground:GetActiveWindow();}
static BOOL SyntheticActivate(HWND window){SetActiveWindow(window);return GetActiveWindow()==window;}
static HWND synthetic_edit{};
static unsigned paste_dispatches{};
static SHORT synthetic_keys[256]{};
static SHORT SyntheticKeyState(int key){return key>=0&&key<256?synthetic_keys[key]:0;}
static UINT SyntheticPasteInput(UINT count,LPINPUT inputs,int bytes){
    if(count!=4||bytes!=sizeof(INPUT)||inputs[0].ki.wVk!=VK_CONTROL||inputs[1].ki.wVk!='V'||inputs[2].ki.wVk!='V'||!(inputs[2].ki.dwFlags&KEYEVENTF_KEYUP)||inputs[3].ki.wVk!=VK_CONTROL||!(inputs[3].ki.dwFlags&KEYEVENTF_KEYUP))return 0;
    if(!synthetic_edit||SyntheticForeground()!=GetAncestor(synthetic_edit,GA_ROOT))return 0;
    GUITHREADINFO info{sizeof(info)};
    if(!GetGUIThreadInfo(GetWindowThreadProcessId(synthetic_edit,nullptr),&info)||info.hwndFocus!=synthetic_edit)return 0;
    SendMessageW(synthetic_edit,WM_PASTE,0,0);++paste_dispatches;return count;
}
#define SendInput SyntheticPasteInput
#define GetAsyncKeyState SyntheticKeyState
#define GetKeyState SyntheticKeyState
#define GetForegroundWindow SyntheticForeground
#define SetForegroundWindow SyntheticActivate
#include "../src/clipboard/panel.cpp"
#undef SendInput
#undef GetAsyncKeyState
#undef GetKeyState
#undef GetForegroundWindow
#undef SetForegroundWindow
struct IsolatedDesktop {
    HWINSTA original{GetProcessWindowStation()},station{};
    HDESK original_desktop{GetThreadDesktop(GetCurrentThreadId())},desktop{};
    bool Init(){station=CreateWindowStationW(nullptr,0,WINSTA_ALL_ACCESS,nullptr);if(!station||!SetProcessWindowStation(station))return false;desktop=CreateDesktopW(L"LumaShotPasteFixture",nullptr,nullptr,0,GENERIC_ALL,nullptr);return desktop&&SetThreadDesktop(desktop);}
    ~IsolatedDesktop(){SetThreadDesktop(original_desktop);SetProcessWindowStation(original);if(desktop)CloseDesktop(desktop);if(station)CloseWindowStation(station);}
};
namespace lumashot {
struct ClipboardPanelTest {
static int Run(){
    int failures{};const auto expect=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<std::endl;failures+=!ok;};
    const auto pump=[] {MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}};
    const auto make=[] {return CreateWindowExW(0,L"STATIC",L"Owned synthetic chat",WS_POPUP|WS_VISIBLE,20,20,400,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);};
    HWND first=make(),second=make();
    HWND edit1=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_MULTILINE,0,0,380,180,first,nullptr,GetModuleHandleW(nullptr),nullptr);
    HWND edit2=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_MULTILINE,0,0,380,180,second,nullptr,GetModuleHandleW(nullptr),nullptr);
    HWND sibling_edit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_MULTILINE,0,180,380,20,first,nullptr,GetModuleHandleW(nullptr),nullptr);
    const auto activate=[&](HWND root,HWND edit){SyntheticActivate(root);SetFocus(edit);synthetic_edit=edit;pump();expect(SyntheticForeground()==root,"fixture input is foreground");};
    const auto text=[](HWND edit){wchar_t value[256]{};SendMessageW(edit,WM_GETTEXT,256,reinterpret_cast<LPARAM>(value));return std::wstring(value);};
    {
        ClipboardPanel panel(nullptr,[]{});auto& p=*panel.impl_;expect(p.Create(),"create production panel without starting listener or reading user history");
        p.enabled=p.expanded=p.pinned=true;p.scale=1;p.UpdateActivationPolicy();
        expect((GetWindowLongPtrW(p.window,GWL_EXSTYLE)&WS_EX_NOACTIVATE)!=0,"fixed panel uses OS nonactivation style");
        expect(SendMessageW(p.window,WM_MOUSEACTIVATE,0,MAKELPARAM(HTCLIENT,WM_LBUTTONDOWN))==MA_NOACTIVATE,"all fixed panel clicks preserve foreground even before hit rectangles exist");
        SetWindowPos(p.window,HWND_TOPMOST,450,20,400,744,SWP_NOACTIVATE|SWP_SHOWWINDOW);
        const auto add=[&](const std::wstring& value){clipboard::Entry entry;entry.kind=clipboard::Kind::Text;entry.text=value;const auto* bytes=reinterpret_cast<const unsigned char*>(value.c_str());entry.formats={{CF_UNICODETEXT,{bytes,bytes+(value.size()+1)*sizeof(wchar_t)}}};p.history.Add(std::move(entry));return p.history.entries.front().id;};
        const auto alpha=add(L"Alpha "),beta=add(L"Beta");p.Filter();
        const auto click=[&](uint64_t id){p.hits={{{20,210,380,290},30,id}};const auto old=paste_dispatches;SendMessageW(p.window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(80,230));expect(paste_dispatches==old&&!p.paste_inflight,"mouse down does not paste or move target focus");SendMessageW(p.window,WM_LBUTTONUP,0,MAKELPARAM(80,230));p.PasteTick();pump();};
        activate(first,edit1);p.previous=nullptr;click(alpha);
        expect(text(edit1)==L"Alpha "&&p.previous==first&&p.expanded,"click with missing old target inserts into current focused edit and stays open");
        click(beta);p.PasteTick();expect(text(edit1)==L"Alpha Beta"&&p.expanded,"second click appends at retained caret without reopening");
        activate(second,edit2);p.previous=first;click(beta);
        expect(text(edit1)==L"Alpha Beta"&&text(edit2)==L"Beta"&&p.previous==second,"stale previous window is replaced by the current input target");
        p.hits={{{20,210,380,290},30,alpha}};SendMessageW(p.window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(80,230));SendMessageW(p.window,WM_LBUTTONUP,0,MAKELPARAM(390,300));expect(text(edit2)==L"Beta"&&!p.paste_inflight,"dragging away cancels row click");
        activate(first,edit1);p.Copy(alpha,true);activate(second,edit2);const auto count=paste_dispatches;p.PasteTick();
        expect(!p.paste_inflight&&paste_dispatches==count&&text(edit2)==L"Beta","switching target during pending paste cancels instead of inserting into another app");
        expect(paste_dispatches==3,"exactly three native pastes and no Enter events");
        const auto key_down=[&](WPARAM key,bool repeat=false){synthetic_keys[key]=static_cast<SHORT>(0x8000);SendMessageW(p.window,WM_KEYDOWN,key,1LL|(repeat?(1LL<<30):0));};
        const auto key_up=[&](WPARAM key){synthetic_keys[key]=0;SendMessageW(p.window,WM_KEYUP,key,1LL|(1LL<<30)|(1LL<<31));};
        const auto key=[&](WPARAM value){key_down(value);key_up(value);};
        const auto finish_keyboard=[&]{p.PasteTick();expect(p.paste_inflight&&p.paste_returning,"keyboard paste waits before restoring panel focus");p.paste_return_at=0;p.PasteTick();pump();expect(!p.paste_inflight&&SyntheticForeground()==p.window&&GetFocus()==p.window,"keyboard paste restores panel keyboard focus");};
        SetWindowTextW(edit1,L"");activate(first,edit1);p.pinned=false;p.UpdateActivationPolicy();p.Show();SetFocus(p.window);
        expect(p.previous_focus==edit1,"opening panel remembers exact input control with another edit in same root");
        key(VK_F2);expect(p.pinned&&GetFocus()==p.window,"keyboard F2 starts continuous paste with retained focus");
        key(VK_HOME);expect(p.selected==beta,"Home selects first history row");
        key(VK_DOWN);expect(p.selected==alpha,"Down selects second history row");
        const auto keyboard_start=paste_dispatches;key_down(VK_RETURN);p.PasteTick();
        expect(p.copy_focus==edit1&&p.paste_focus==edit1,"keyboard request snapshots original edit control");
        expect(paste_dispatches==keyboard_start&&SyntheticForeground()==p.window,"held Enter does not activate target or inject paste");
        key_down(VK_RETURN,true);p.PasteTick();expect(paste_dispatches==keyboard_start,"Enter auto-repeat cannot start duplicate paste");
        key_up(VK_RETURN);finish_keyboard();expect(text(edit1)==L"Alpha ","first keyboard selection pastes at target caret");
        key(VK_UP);key(VK_RETURN);finish_keyboard();expect(text(edit1)==L"Alpha Beta"&&paste_dispatches==keyboard_start+2,"Up and Enter append second item without reopening");
        expect(text(sibling_edit).empty(),"restored input focus does not paste into sibling edit");
        activate(first,sibling_edit);p.Show();
        expect(p.previous_focus==sibling_edit,"selecting another input in same root refreshes remembered control");
        key(VK_HOME);key(VK_RETURN);finish_keyboard();
        expect(text(sibling_edit)==L"Beta"&&text(edit1)==L"Alpha Beta","new same-root target receives paste without altering prior edit");
        activate(first,edit1);p.Show();
        const auto navigation_start=paste_dispatches;
        for(const auto navigation:{VK_HOME,VK_END,VK_PRIOR,VK_NEXT}){key(static_cast<WPARAM>(navigation));p.PasteTick();expect(!p.paste_inflight&&paste_dispatches==navigation_start,"navigation keys only select while fixed");}
        auto* rich=p.history.Find(alpha);const UINT rich_format=RegisterClipboardFormatW(L"LumaShot synthetic rich text");
        rich->formats.push_back({rich_format,{1,2,3,4}});p.selected=alpha;
        key_down(VK_SHIFT);key_down(VK_RETURN);key_up(VK_RETURN);p.PasteTick();
        expect(paste_dispatches==navigation_start&&SyntheticForeground()==p.window,"held Shift delays target activation");
        key_up(VK_SHIFT);finish_keyboard();
        expect(IsClipboardFormatAvailable(CF_UNICODETEXT)&&!IsClipboardFormatAvailable(rich_format),"Shift Enter publishes text without rich custom format");
        expect(text(edit1)==L"Alpha BetaAlpha ","Shift Enter inserts plain text once");
        key(VK_RETURN);p.PasteTick();const auto before_return=paste_dispatches;activate(second,edit2);p.paste_return_at=0;p.PasteTick();
        expect(!p.paste_inflight&&SyntheticForeground()==second&&paste_dispatches==before_return,"user window switch during return never steals focus or injects twice");
        SetWindowTextW(edit1,L"Alpha Beta");SetWindowTextW(edit2,L"Beta");
        wchar_t executable[MAX_PATH]{},station_name[256]{},desktop_name[256]{};DWORD needed{};
        GetModuleFileNameW(nullptr,executable,MAX_PATH);
        GetUserObjectInformationW(GetProcessWindowStation(),UOI_NAME,station_name,sizeof(station_name),&needed);
        GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,desktop_name,sizeof(desktop_name),&needed);
        std::wstring desktop_path=std::wstring(station_name)+L"\\"+desktop_name;
        std::wstring command=L"\""+std::wstring(executable)+L"\" --child";
        STARTUPINFOW startup{sizeof(startup)};startup.lpDesktop=desktop_path.data();PROCESS_INFORMATION child{};
        const bool started=CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&child)!=FALSE;
        expect(started,"start independent input process on private clipboard station");
        if(started){
            HWND remote{};for(int i=0;i<100&&!remote;++i){remote=FindWindowW(L"STATIC",L"LumaShot independent paste fixture");if(!remote)Sleep(20);}
            expect(remote!=nullptr,"independent input window exists");
            if(remote){
                external_foreground=remote;synthetic_edit=FindWindowExW(remote,nullptr,L"EDIT",nullptr);p.previous=nullptr;p.previous_process=0;
                ClipboardPanel::Impl::ForegroundChanged(p.foreground_hook,EVENT_SYSTEM_FOREGROUND,remote,OBJID_WINDOW,0,child.dwThreadId,0);
                expect(p.previous==remote&&p.previous_process==child.dwProcessId,"foreground observer retains external process identity without panel activation");
                click(alpha);click(beta);
                expect(text(synthetic_edit)==L"Alpha Beta","successive clicks paste into independent process edit");
                const auto store_root=std::filesystem::path(executable).parent_path()/L"continuous-paste-fixture";
                p.disk=std::make_unique<clipboard::SessionStore>(store_root,p.window,false);p.disk->WaitIdle();p.DrainStore();
                auto stored=*p.history.Find(alpha);stored.id=0;expect(p.disk->Save(std::move(stored))&&p.disk->WaitIdle(),"save synthetic encrypted clipboard payload");p.DrainStore();
                const auto stored_id=p.history.entries.front().id;expect(p.history.entries.front().payload!=nullptr,"fixture uses disk-backed history path");
                p.Copy(stored_id,true);expect(p.copy_pending&&p.copy_target==remote&&p.copy_process==child.dwProcessId,"async load snapshots external destination");
                p.disk->WaitIdle();p.DrainStore();p.PasteTick();
                expect(text(synthetic_edit)==L"Alpha BetaAlpha ","encrypted async history inserts into independent process");
                p.Copy(stored_id,true);external_foreground=first;p.disk->WaitIdle();p.DrainStore();
                expect(!p.paste_inflight&&text(synthetic_edit)==L"Alpha BetaAlpha ","target switch during disk read cancels delivery");

                external_foreground=nullptr;activate(first,edit1);p.RememberTarget(first);SyntheticActivate(p.window);SetFocus(p.window);p.selected=stored_id;
                const auto async_before=paste_dispatches;key_down(VK_SHIFT);key(VK_RETURN);
                expect(p.copy_pending,"keyboard Shift Enter loads encrypted payload asynchronously");
                key_up(VK_SHIFT);p.disk->WaitIdle();p.DrainStore();finish_keyboard();
                expect(paste_dispatches==async_before+1&&IsClipboardFormatAvailable(CF_UNICODETEXT)&&!IsClipboardFormatAvailable(rich_format),"async keyboard request retains plain text and focus return after Shift release");

                external_foreground=nullptr;PostMessageW(remote,WM_CLOSE,0,0);
            }
            if(WaitForSingleObject(child.hProcess,3000)!=WAIT_OBJECT_0)TerminateProcess(child.hProcess,1);
            CloseHandle(child.hThread);CloseHandle(child.hProcess);
        }
        activate(first,edit1);
        expect(p.expanded&&p.pinned,"fixed panel is expanded while external input retains focus");
        p.hits={{{270,24,302,56},10,0}};
        expect(SendMessageW(p.window,WM_MOUSEACTIVATE,0,MAKELPARAM(HTCLIENT,WM_LBUTTONDOWN))==MA_NOACTIVATE,"unpin click does not first activate fixed panel");
        SendMessageW(p.window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(286,40));
        SendMessageW(p.window,WM_LBUTTONUP,0,MAKELPARAM(286,40));
        expect(!p.pinned&&p.expanded&&SyntheticForeground()==p.window,"mouse unpin activates panel and keeps it expanded");
        expect(!(GetWindowLongPtrW(p.window,GWL_EXSTYLE)&WS_EX_NOACTIVATE),"unpin restores normal activation");
        activate(first,edit1);expect(!p.expanded,"mouse unpin folds only on subsequent deactivation");
        p.Show();p.Action(10,0);SyntheticActivate(p.window);p.Action(10,0);
        expect(!p.pinned&&p.expanded,"unpin while panel has keyboard focus stays expanded");
        activate(first,edit1);expect(!p.expanded,"focused unpinned panel folds on next deactivation");
        p.Show();SetFocus(p.window);
        SendMessageW(p.window,WM_KEYDOWN,VK_F2,1);
        expect(p.pinned&&p.expanded,"F2 fixes focused panel");
        expect(SyntheticForeground()==p.window&&GetFocus()==p.window,"F2 pin retains keyboard focus for the next F2");
        SendMessageW(p.window,WM_KEYDOWN,VK_F2,1);
        expect(!p.pinned&&p.expanded,"second F2 unpins without any intervening activation");
        SendMessageW(p.window,WM_KEYDOWN,VK_F2,1);
        expect(p.pinned&&SyntheticForeground()==p.window,"third F2 pins and retains focus");
        SendMessageW(p.window,WM_KEYDOWN,VK_F2,1LL|(1LL<<30));
        expect(p.pinned,"holding F2 does not repeatedly toggle panel");
        ShowWindow(p.search,SW_SHOW);SetFocus(p.search);SetWindowTextW(p.search,L"fixture query");
        SendMessageW(p.search,WM_KEYDOWN,VK_F2,1);
        expect(!p.pinned&&p.expanded&&p.query==L"fixture query","F2 in search unpins without changing search text");
        SendMessageW(p.search,WM_KEYDOWN,VK_F2,1LL|(1LL<<30));
        expect(!p.pinned,"search ignores F2 key repeat");
        SendMessageW(p.window,WM_TIMER,6,0);expect(p.status.empty(),"F2 status clears after transient notification");
        SyntheticActivate(p.window);SetFocus(p.search);SendMessageW(p.search,WM_KEYDOWN,VK_F2,1);
        expect(p.pinned,"search F2 also enables pin");
        activate(first,edit1);SendMessageW(edit1,WM_KEYDOWN,VK_F2,1);
        expect(p.pinned,"external input F2 does not toggle clipboard");
        SendMessageW(p.window,WM_KEYDOWN,VK_F2,1);
        expect(p.pinned,"background panel rejects shortcut even if sent a key message");

    }
    DestroyWindow(first);DestroyWindow(second);return failures?1:0;
}
};
}
int main(int argc,char**){if(argc>1){
    HWND root=CreateWindowExW(0,L"STATIC",L"LumaShot independent paste fixture",WS_POPUP|WS_VISIBLE,20,20,400,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    HWND edit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_MULTILINE,0,0,380,180,root,nullptr,GetModuleHandleW(nullptr),nullptr);SetFocus(edit);
    MSG message{};while(IsWindow(root)){if(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}else Sleep(5);}return 0;
}IsolatedDesktop isolation;if(!isolation.Init()){std::cerr<<"Cannot create private clipboard desktop\n";return 1;}CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);const int result=lumashot::ClipboardPanelTest::Run();CoUninitialize();return result;}
