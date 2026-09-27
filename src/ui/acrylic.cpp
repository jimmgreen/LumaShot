#include "ui/acrylic.h"
#include <cstring>
#include <dwmapi.h>
#pragma comment(lib,"dwmapi.lib")
namespace lumashot {
void ConfigureBorderlessWindow(HWND window){
    SetWindowLongPtrW(window,GWL_STYLE,(GetWindowLongPtrW(window,GWL_STYLE)&~(WS_CAPTION|WS_THICKFRAME|WS_BORDER|DS_MODALFRAME))|WS_POPUP);
    SetWindowLongPtrW(window,GWL_EXSTYLE,(GetWindowLongPtrW(window,GWL_EXSTYLE)&~(WS_EX_DLGMODALFRAME|WS_EX_WINDOWEDGE|WS_EX_CLIENTEDGE))|WS_EX_LAYERED);
    const DWORD round=2;DwmSetWindowAttribute(window,33,&round,sizeof(round));
    SetWindowPos(window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
}
void ResizeBorderlessWindow(HWND window,float scale,float radius){
    RECT client{};GetClientRect(window,&client);HRGN region=CreateRoundRectRgn(0,0,client.right+1,client.bottom+1,int(2*radius*scale),int(2*radius*scale));if(region&&!SetWindowRgn(window,region,TRUE))DeleteObject(region);
}
bool SetSettingsAcrylic(HWND window,bool dark){
    // Runtime lookup keeps the app compatible with Windows builds without this
    // composition entry point. The caller renders an opaque fallback on failure.
    struct Accent {int state,flags;DWORD gradient;int animation;};
    struct Attribute {int attribute;void* data;SIZE_T size;};
    using Apply=BOOL(WINAPI*)(HWND,Attribute*);
    const auto address=GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetWindowCompositionAttribute");
    Apply apply{};static_assert(sizeof(apply)==sizeof(address));std::memcpy(&apply,&address,sizeof(apply));
    if(!apply)return false;
    Accent accent{4,0,dark?0xb3382c25u:0xb3fcf9f7u,0};
    Attribute data{19,&accent,sizeof(accent)};
    return apply(window,&data)!=FALSE;
}
}


