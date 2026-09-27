#include "clipboard/panel.h"
#include "clipboard/win_v_shortcut.h"
#include "ui/shortcut_label.h"
#include "clipboard/composition.h"
#include "clipboard/native.h"
#include "clipboard/file_icons.h"
#include "clipboard/session_store.h"
#include "clipboard/layout.h"
#include "clipboard/preview_window.h"
#include "clipboard/preview_cache.h"
#include "ui/acrylic.h"
#include "ui/brand_icon.h"
#include "ui/text_renderer.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <wincodec.h>
#include "capture/frame.h"
#include <commctrl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <imm.h>
#include <map>
#include <set>
#include <cmath>


// Live keyboard/foreground test. Publication is entirely synthetic: this test
// never opens, reads, or writes the user's clipboard and starts no listener.
#include <iostream>
#include <cstring>
static std::wstring fixture_payload,fixture_text;
static unsigned fixture_pastes{},fixture_enters{};
static unsigned fixture_focus_losses{};
static bool fixture_plain{};
static unsigned fixture_writes{};
static DWORD fixture_activation_error{};
static HWND fixture_target{};
namespace lumashot::clipboard {
static bool FixtureWrite(HWND owner,const Entry& entry,bool plain=false){
    fixture_plain=plain;++fixture_writes;
    COPYDATASTRUCT data{};data.cbData=static_cast<DWORD>((entry.text.size()+1)*sizeof(wchar_t));data.lpData=const_cast<wchar_t*>(entry.text.c_str());DWORD_PTR result{};
    return SendMessageTimeoutW(fixture_target,WM_COPYDATA,reinterpret_cast<WPARAM>(owner),reinterpret_cast<LPARAM>(&data),SMTO_ABORTIFHUNG,500,&result)&&result;
}
}
static DWORD FixtureSequence(){return 7;}
#define Write FixtureWrite
#define GetClipboardSequenceNumber FixtureSequence
#include "../src/clipboard/panel.cpp"
#undef Write
#undef GetClipboardSequenceNumber
static LRESULT CALLBACK FixtureProc(HWND window,UINT message,WPARAM key,LPARAM data){
    if(message==WM_COPYDATA){const auto* payload=reinterpret_cast<const COPYDATASTRUCT*>(data);if(!payload||payload->cbData<sizeof(wchar_t)||payload->cbData%sizeof(wchar_t))return FALSE;const auto* value=static_cast<const wchar_t*>(payload->lpData);const auto size=payload->cbData/sizeof(wchar_t);if(value[size-1])return FALSE;fixture_payload.assign(value,size-1);return TRUE;}
    if(message==WM_GETTEXT){if(!key)return 0;const auto length=std::min(fixture_text.size(),static_cast<size_t>(key-1));auto* output=reinterpret_cast<wchar_t*>(data);std::copy_n(fixture_text.data(),length,output);output[length]=0;return static_cast<LRESULT>(length);}
    if(message==WM_APP+1)return fixture_pastes;
    if(message==WM_APP+2)return fixture_enters;
    if(message==WM_APP+3)return fixture_focus_losses;
    if(message==WM_APP+4){
        HWND child=CreateWindowExW(0,L"LumaShotKeyboardLiveFixture",L"",WS_CHILD|WS_VISIBLE,10,10,180,30,window,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(child)SetFocus(child);
        return reinterpret_cast<LRESULT>(child);
    }
    if(message==WM_APP+5){
        SetLastError(ERROR_SUCCESS);
        const bool activated=SetForegroundWindow(window)!=FALSE;
        fixture_activation_error=GetLastError();
        if(GetForegroundWindow()==window)SetFocus(window);
        return (activated?1:0)|(GetForegroundWindow()==window?2:0)|(GetFocus()==window?4:0);
    }
    if(message==WM_APP+6)return fixture_activation_error;
    if(message==WM_KILLFOCUS||(message==WM_ACTIVATE&&LOWORD(key)==WA_INACTIVE))++fixture_focus_losses;
    if(message==WM_CLOSE){DestroyWindow(window);return 0;}
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
    if((message==WM_KEYDOWN||message==WM_KEYUP)&&key==VK_RETURN){++fixture_enters;return 0;}
    if(message==WM_KEYDOWN&&key=='V'&&(GetKeyState(VK_CONTROL)&0x8000)){
        fixture_text+=fixture_payload;++fixture_pastes;return 0;
    }
    // Even a stray paste message must never access the system clipboard.
    if(message==WM_PASTE)return 0;
    return DefWindowProcW(window,message,key,data);
}
namespace lumashot {
struct ClipboardPanelTest {
static int Run(bool quick_mode=false){
    int failures{};
    const HWND original_foreground=GetForegroundWindow();
    const auto expect=[&](bool value,const char* label){std::cout<<(value?"PASS ":"FAIL ")<<label<<std::endl;failures+=!value;};
    wchar_t executable[MAX_PATH]{};GetModuleFileNameW(nullptr,executable,MAX_PATH);
    std::wstring command=L"\""+std::wstring(executable)+L"\" --child";
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION child{};
    if(!CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&child))return 1;
    WaitForInputIdle(child.hProcess,1000);
    HWND target{};const auto startup_deadline=GetTickCount64()+3000;
    while(!target&&GetTickCount64()<startup_deadline){HWND candidate=FindWindowW(L"LumaShotKeyboardLiveFixture",nullptr);DWORD process{};if(candidate&&GetWindowThreadProcessId(candidate,&process)&&process==child.dwProcessId)target=candidate;else Sleep(10);}
    fixture_target=target;
    const auto pastes=[&]{return static_cast<unsigned>(SendMessageW(target,WM_APP+1,0,0));};
    const auto target_text=[&]{wchar_t output[256]{};SendMessageW(target,WM_GETTEXT,256,reinterpret_cast<LPARAM>(output));return std::wstring(output);};
    expect(target!=nullptr,"create independent synthetic input process");
    if(target)
    {
        ClipboardPanel panel(nullptr,[]{});auto& p=*panel.impl_;
        expect(p.Create(),"create production panel without clipboard listener or preferences");
        if(p.window){
            p.enabled=true;p.pinned=false;p.expanded=false;p.scale=1;
            const auto add=[&](const wchar_t* text){clipboard::Entry entry;entry.kind=clipboard::Kind::Text;entry.text=text;const auto* bytes=reinterpret_cast<const unsigned char*>(entry.text.c_str());entry.formats={{CF_UNICODETEXT,{bytes,bytes+(entry.text.size()+1)*sizeof(wchar_t)}}};p.history.Add(std::move(entry));return p.history.entries.front().id;};
            const auto alpha=add(L"Alpha "),beta=add(L"Beta");
            const ULONGLONG deadline=GetTickCount64()+(quick_mode?10000:5000);
            bool canceled=false;
            const auto owned=[&]{const HWND foreground=GetForegroundWindow();return foreground==target||(!quick_mode&&foreground==p.window);};
            const auto cancel=[&]{canceled=true;KillTimer(p.window,2);p.paste_inflight=false;p.copy_pending=false;};
            const auto pump=[&]{MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
                if(!owned()){cancel();return;}
                TranslateMessage(&message);DispatchMessageW(&message);
            }};
            const auto wait=[&](auto condition){while(!canceled&&GetTickCount64()<deadline){if(!owned()){cancel();break;}pump();if(condition())return true;Sleep(5);}return false;};
            const auto key=[&](WORD value){
                if(canceled||!owned()||GetForegroundWindow()!=p.window){cancel();return false;}
                INPUT input[2]{};for(auto& item:input){item.type=INPUT_KEYBOARD;item.ki.wVk=value;}input[1].ki.dwFlags=KEYEVENTF_KEYUP;
                return SendInput(2,input,sizeof(INPUT))==2;
            };
            // Startup only: allow this synthetic harness to acquire the desktop's
            // foreground input queue. Detach before pumping or checking behavior.
            // Never retry activation after the actual focus-preservation test starts.
            SetLastError(ERROR_SUCCESS);
            const bool allowed=AllowSetForegroundWindow(child.dwProcessId)!=FALSE;
            const DWORD allow_error=GetLastError();
            DWORD_PTR child_activation{};
            const bool child_replied=SendMessageTimeoutW(target,WM_APP+5,0,0,SMTO_ABORTIFHUNG,500,&child_activation)!=0;
            std::cout<<"STARTUP allow="<<allowed<<" error="<<allow_error<<" child_replied="<<child_replied<<" child_flags="<<child_activation<<" child_error="<<SendMessageW(target,WM_APP+6,0,0)<<std::endl;
            SetForegroundWindow(target);
            if(GetForegroundWindow()!=target){
                const HWND foreground=GetForegroundWindow();
                const DWORD foreground_thread=foreground?GetWindowThreadProcessId(foreground,nullptr):0;
                const DWORD current_thread=GetCurrentThreadId();
                const DWORD target_thread=GetWindowThreadProcessId(target,nullptr);
                SetLastError(ERROR_SUCCESS);
                const bool attached=foreground_thread&&foreground_thread!=current_thread&&AttachThreadInput(current_thread,foreground_thread,TRUE)!=FALSE;
                const DWORD attach_error=GetLastError();
                SetLastError(ERROR_SUCCESS);
                const bool target_attached=target_thread!=current_thread&&target_thread!=foreground_thread&&AttachThreadInput(current_thread,target_thread,TRUE)!=FALSE;
                const DWORD target_attach_error=GetLastError();
                SetLastError(ERROR_SUCCESS);
                const bool set_foreground=SetForegroundWindow(target)!=FALSE;
                const DWORD foreground_error=GetLastError();
                if(target_attached)AttachThreadInput(current_thread,target_thread,FALSE);
                if(attached)AttachThreadInput(current_thread,foreground_thread,FALSE);
                std::cout<<"STARTUP foreground_null="<<(foreground==nullptr)<<" current_thread="<<current_thread<<" foreground_thread="<<foreground_thread<<" target_thread="<<target_thread<<" attached="<<attached<<" attach_error="<<attach_error<<" target_attached="<<target_attached<<" target_attach_error="<<target_attach_error<<" set_foreground="<<set_foreground<<" foreground_error="<<foreground_error<<std::endl;
                if(GetForegroundWindow()==target)SendMessageTimeoutW(target,WM_APP+5,0,0,SMTO_ABORTIFHUNG,500,&child_activation);
            }
            if(GetForegroundWindow()!=target){
                SwitchToThisWindow(target,TRUE);
                SendMessageTimeoutW(target,WM_APP+5,0,0,SMTO_ABORTIFHUNG,500,&child_activation);
                std::cout<<"STARTUP explicit_fixture_switch="<<(GetForegroundWindow()==target)<<std::endl;
            }
            const auto activation_deadline=GetTickCount64()+500;
            while(GetForegroundWindow()!=target&&GetTickCount64()<activation_deadline){
                MSG message{};
                while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
                Sleep(5);
            }
            const bool activated=GetForegroundWindow()==target;
            if(!activated)std::cout<<"BLOCKED: synthetic target could not acquire foreground; final_foreground_null="<<(GetForegroundWindow()==nullptr)<<std::endl;
            expect(activated&&GetForegroundWindow()==target,"activate owned synthetic target using real foreground API");
            if(activated&&GetForegroundWindow()==target){
                if(quick_mode){
                    const auto focused=[&]{GUITHREADINFO info{sizeof(info)};return GetForegroundWindow()==target&&GetGUIThreadInfo(GetWindowThreadProcessId(target,nullptr),&info)&&info.hwndFocus==target;};
                    const bool ready=wait(focused);
                    const auto losses=SendMessageW(target,WM_APP+3,0,0);
                    const auto post=[&](WPARAM value,bool shift=false){return !canceled&&focused()&&PostMessageW(p.window,clipboard::QuickKeyMessage,value,shift?1:0)!=FALSE;};
                    expect(ready,"synthetic target owns foreground and keyboard focus before quick popup");
                    if(ready){
                        p.ShowQuick();
                        const bool shown=wait([&]{return p.quick_active&&p.quick&&p.quick->Visible()&&focused();});
                        expect(shown&&!p.expanded,"quick popup opens without activating full panel or changing input focus");
                        expect(shown&&!p.quick_continuous,"quick popup initially uses single insertion mode");
                        const bool category=shown&&post(VK_RIGHT)&&wait([&]{return p.quick_tab==1&&p.quick_ids.empty();});
                        expect(category&&focused(),"quick right selects empty favorites category without changing focus");
                        const bool all=category&&post(VK_LEFT)&&wait([&]{return p.quick_tab==0&&p.quick_ids.size()==2;});
                        expect(all,"quick left restores all records");
                        const bool fixed=all&&post(VK_F2)&&wait([&]{return p.quick_continuous;});
                        expect(fixed&&focused(),"quick F2 enables continuous insertion without changing focus");
                        const bool selected=fixed&&post(VK_DOWN)&&wait([&]{return p.quick_selection<p.quick_ids.size()&&p.quick_ids[p.quick_selection]==alpha;});
                        const bool first=selected&&post(VK_RETURN)&&wait([&]{return pastes()==1&&!p.paste_inflight;});
                        expect(first&&target_text()==L"Alpha "&&p.quick_active&&p.quick->Visible()&&focused(),"first continuous insertion keeps popup and original target input focus");
                        const bool up=first&&post(VK_UP)&&wait([&]{return p.quick_selection<p.quick_ids.size()&&p.quick_ids[p.quick_selection]==beta;});
                        const bool second=up&&post(VK_RETURN)&&wait([&]{return pastes()==2&&!p.paste_inflight;});
                        expect(second&&target_text()==L"Alpha Beta"&&p.quick_active&&p.quick->Visible()&&focused(),"second continuous insertion retains target foreground and focus");
                        const bool escaped=second&&post(VK_ESCAPE)&&wait([&]{return !p.quick_active&&!p.quick->Visible();});
                        expect(escaped&&focused(),"quick Escape closes popup without moving target focus");
                        if(escaped){
                            p.ShowQuick();
                            const bool reopened=wait([&]{return p.quick_active&&p.quick->Visible();});
                            const bool normal=reopened&&(!p.quick_continuous||(post(VK_F2)&&wait([&]{return !p.quick_continuous;})));
                            expect(normal,"F2 restores default single insertion mode");
                            const bool inserted=normal&&post(VK_RETURN)&&wait([&]{return pastes()==3&&!p.paste_inflight&&!p.quick_active;});
                            expect(inserted&&!p.quick->Visible()&&target_text()==L"Alpha BetaBeta"&&focused(),"default insertion closes popup and preserves target input focus");
                            expect(inserted&&!fixture_plain,"ordinary Enter publishes with original-format flag");
                            if(inserted){
                                p.ShowQuick();
                                const bool plain_shown=wait([&]{return p.quick_active&&p.quick->Visible();});
                                const auto writes=fixture_writes;
                                const bool plain_inserted=plain_shown&&post(VK_RETURN,true)&&wait([&]{return pastes()==4&&!p.paste_inflight&&!p.quick_active;});
                                expect(plain_inserted&&fixture_writes==writes+1&&fixture_plain&&target_text()==L"Alpha BetaBetaBeta"&&focused(),"Shift Enter publishes plain-text flag and inserts without changing focus");
                            }
                        }
                        expect(SendMessageW(target,WM_APP+3,0,0)==losses,"target received no focus-loss or deactivation event during quick session");
                        expect(SendMessageW(target,WM_APP+2,0,0)==0,"quick routing never delivers Enter down or up to target");
                        if(!canceled&&!p.quick_active&&focused()){
                            p.ShowQuick();
                            const bool focus_shown=wait([&]{return p.quick_active&&p.quick->Visible();});
                            HWND child_focus=focus_shown?reinterpret_cast<HWND>(SendMessageW(target,WM_APP+4,0,0)):nullptr;
                            const bool guarded=child_focus&&wait([&]{return !p.quick_active&&!p.quick->Visible();});
                            GUITHREADINFO info{sizeof(info)};
                            expect(guarded&&GetForegroundWindow()==target&&GetGUIThreadInfo(GetWindowThreadProcessId(target,nullptr),&info)&&info.hwndFocus==child_focus,"session timer dismisses popup when focus changes inside same foreground process");
                        }
                    }
                }else{
                p.Show();
                const bool shown=wait([&]{return GetForegroundWindow()==p.window&&GetFocus()==p.window;});
                expect(shown,"show panel takes real keyboard focus");
                if(shown){
                    const bool fixed=key(VK_F2)&&wait([&]{return p.pinned;});
                    expect(fixed,"real F2 input fixes panel");
                    const bool selected=fixed&&key(VK_DOWN)&&wait([&]{return p.selected==alpha;});
                    expect(selected,"real Down input selects second entry");
                    const bool first=selected&&key(VK_RETURN)&&wait([&]{return pastes()==1&&!p.paste_inflight&&GetForegroundWindow()==p.window&&GetFocus()==p.window;});
                    expect(first&&target_text()==L"Alpha ","real Ctrl V reaches target and panel regains foreground after first Enter");
                    const bool up=first&&key(VK_UP)&&wait([&]{return p.selected==beta;});
                    const bool second=up&&key(VK_RETURN)&&wait([&]{return pastes()==2&&!p.paste_inflight&&GetForegroundWindow()==p.window&&GetFocus()==p.window;});
                    expect(second&&target_text()==L"Alpha Beta","real Up Enter sends second paste and restores panel keyboard focus");
                    expect(SendMessageW(target,WM_APP+2,0,0)==0,"triggering Enter down and up never reach synthetic target");
                }
                }
            }
            expect(!canceled,"user did not switch away from owned test windows");cancel();
            const HWND ending_foreground=GetForegroundWindow();
            if(IsWindow(original_foreground)&&(ending_foreground==target||ending_foreground==p.window))SetForegroundWindow(original_foreground);
        }
    }
    if(target)PostMessageW(target,WM_CLOSE,0,0);
    if(WaitForSingleObject(child.hProcess,1000)!=WAIT_OBJECT_0)TerminateProcess(child.hProcess,1);
    CloseHandle(child.hThread);CloseHandle(child.hProcess);return failures?1:0;
}
};
}
int main(int argc,char** argv){
    if(argc>1&&std::strcmp(argv[1],"--child")==0){
        WNDCLASSW type{};type.lpfnWndProc=FixtureProc;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"LumaShotKeyboardLiveFixture";
        if(!RegisterClassW(&type))return 1;
        HWND target=CreateWindowExW(0,type.lpszClassName,L"LumaShot synthetic keyboard target",WS_OVERLAPPEDWINDOW|WS_VISIBLE,40,40,440,260,nullptr,nullptr,type.hInstance,nullptr);
        if(!target)return 1;
        SetFocus(target);MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}return 0;
    }
    HDESK input=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);
    wchar_t current_name[256]{},input_name[256]{};DWORD needed{};
    const bool desktop_ok=input&&GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,current_name,sizeof(current_name),&needed)&&GetUserObjectInformationW(input,UOI_NAME,input_name,sizeof(input_name),&needed)&&wcscmp(current_name,input_name)==0;
    if(input)CloseDesktop(input);
    if(!desktop_ok){std::cerr<<"Live test requires the current input desktop\n";return 1;}
    const bool quick_mode=argc>1&&std::strcmp(argv[1],"--quick")==0;
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);const int result=lumashot::ClipboardPanelTest::Run(quick_mode);CoUninitialize();return result;
}
