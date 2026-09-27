#include "ui/themed_message.h"
#include <iostream>
using namespace lumashot;
namespace {
int mode{},failures{},seen{};HWND owner{};
void Expect(bool value,const char* name){std::cout<<(value?"PASS ":"FAIL ")<<name<<'\n';if(!value)++failures;}
void CALLBACK Tick(HWND,UINT,UINT_PTR,DWORD){
    HWND w=FindWindowW(L"LumaShot.ThemedMessage",nullptr);if(!w)return;
    ++seen;Expect(!IsWindowEnabled(owner),"owner disabled during prompt");
    Expect((GetWindowLongPtrW(w,GWL_STYLE)&WS_CAPTION)==0,"no native dialog chrome");
    if(mode==0)PostMessageW(w,WM_KEYDOWN,VK_ESCAPE,0);
    if(mode==1)PostMessageW(w,WM_KEYDOWN,VK_RETURN,0);
    if(mode==2){PostMessageW(w,WM_KEYDOWN,VK_TAB,0);PostMessageW(w,WM_KEYDOWN,VK_RETURN,0);}
    if(mode==3)PostMessageW(w,WM_CLOSE,0,0);
}
}
int main(){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    owner=CreateWindowExW(0,L"STATIC",L"Synthetic test owner",WS_POPUP,100,100,500,300,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    const auto timer=SetTimer(nullptr,0,100,Tick);
    for(bool dark:{false,true})for(mode=0;mode<4;++mode){
        const bool answer=ui::ShowThemedMessage(owner,dark,L"关闭录制？",L"当前录制尚未保存。关闭后将无法恢复这段内容。",L"放弃并关闭",L"保留录制");
        Expect(answer==(mode==2),"only explicit confirmation accepts");Expect(IsWindowEnabled(owner),"owner re-enabled");
        Expect(!FindWindowW(L"LumaShot.ThemedMessage",nullptr),"dialog destroyed");
    }
    mode=1;Expect(!ui::ShowThemedMessage(owner,false,L"导出未完成",L"导出已取消，原始录制仍可继续预览。"),"notice dismisses safely");
    Expect(seen>=9,"all prompts rendered and received input");
    KillTimer(nullptr,timer);DestroyWindow(owner);CoUninitialize();return failures?1:0;
}
