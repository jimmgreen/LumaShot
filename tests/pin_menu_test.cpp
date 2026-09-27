#include "ui/pin_menu.h"
#include "export/png.h"
#include <wincodec.h>
#include <iostream>
using namespace lumashot;
static int requested=0;
static bool alpha_popup=true;
static void CALLBACK Drive(HWND,UINT,UINT_PTR id,DWORD){
    HWND menu=FindWindowW(L"LumaShot.PinMenu",nullptr);if(!menu)return;KillTimer(nullptr,id);
    alpha_popup&=(GetWindowLongPtrW(menu,GWL_EXSTYLE)&WS_EX_LAYERED)!=0&&(GetClassLongPtrW(menu,GCL_STYLE)&CS_DROPSHADOW)==0;
    if(requested==10){const float scale=float(GetDpiForWindow(menu))/96;PinMenuModel m;m.table=true;const LPARAM pt=MAKELPARAM(int((PinMenuModel::Shadow+60)*scale),int((PinMenuModel::Shadow+m.RowTop(5)+21)*scale));PostMessageW(menu,WM_LBUTTONDOWN,0,pt);PostMessageW(menu,WM_LBUTTONUP,0,pt);}
    else if(requested==11)PostMessageW(menu,WM_LBUTTONDOWN,0,MAKELPARAM(1,1));
    else if(requested==12)PostMessageW(menu,WM_KEYDOWN,VK_SPACE,0);
    else if(requested==13)PostMessageW(menu,WM_KEYDOWN,'L',0);
    else if(requested==0)PostMessageW(menu,WM_KEYDOWN,VK_ESCAPE,0);
    else if(requested==14)PostMessageW(menu,WM_LBUTTONDOWN,0,MAKELPARAM(-20,-20));
    else {PostMessageW(menu,WM_KEYDOWN,VK_HOME,0);for(int i=1;i<requested;++i)PostMessageW(menu,WM_KEYDOWN,VK_DOWN,0);PostMessageW(menu,WM_KEYDOWN,VK_RETURN,0);}
}
int main(){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int failures=0;
    auto expect=[&](bool value,const char* message){std::cout<<(value?"PASS ":"FAIL ")<<message<<'\n';failures+=!value;};
    PinMenuModel m;m.table=true;for(float scale:{1.f,1.5f,2.f})for(POINT point:{POINT{-1920,0},POINT{-1,1079}}){auto r=PlacePinMenu(point,{-1920,0,0,1080},scale,m);expect(r.left>=-1920&&r.top>=0&&r.right<=0&&r.bottom<=1080,"menu stays on negative-coordinate monitor at mixed DPI");}
    for(int i=0;i<PinMenuModel::Count;++i)expect(m.Hit(40,m.RowTop(i)+21)==i,"paint and hit rows agree");
    m.enabled[0]=m.enabled[1]=false;expect(m.Next(8,1)==2&&m.Next(2,-1)==8&&m.Hit(40,29)==-1,"disabled rows skipped by mouse and keyboard");m.enabled.fill(true);
    HWND owner=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Synthetic menu test",WS_POPUP,100,100,400,400,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);ShowWindow(owner,SW_SHOWNOACTIVATE);
    for(requested=0;requested<=14;++requested){SetTimer(nullptr,0,30,Drive);int command=TrackPinMenu(owner,{150,150},m);expect(command==(requested>=1&&requested<=9?PinMenuModel::Commands[requested-1]:requested==10?3:requested==12?7:requested==13?8:0),"popup dispatch, Escape and outside-click dismissal");}
    m.table=false;
    expect(m.Next(3,1)==5&&m.Next(5,-1)==3&&m.Hit(40,m.RowTop(5)+21)==5,"table row absent without a reliable result");
    m.table=true;expect(m.Next(3,1)==4&&PinMenuModel::Commands[4]==9,"table command reachable with a reliable result");
    expect(alpha_popup,"popup uses alpha compositing without native hard shadow");DestroyWindow(owner);
    m.recognized=true;
    try{
        auto frame=MakeFrame({0,0,604,530},0xffffffff);ComPtr<IWICImagingFactory> wic;CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic));ComPtr<IWICBitmap> bitmap;
        wic->CreateBitmapFromMemory(frame.Width(),frame.Height(),GUID_WICPixelFormat32bppPBGRA,frame.Width()*4,UINT(frame.pixels.size()*4),reinterpret_cast<BYTE*>(frame.pixels.data()),&bitmap);
        ComPtr<ID2D1Factory> factory;D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf());ComPtr<ID2D1RenderTarget> target;
        factory->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&target);
        target->BeginDraw();target->SetTransform(D2D1::Matrix3x2F::Translation(24,32));DrawPinMenuSurface(target.Get(),m,2);m.dark=true;m.locked=true;target->SetTransform(D2D1::Matrix3x2F::Translation(316,32));DrawPinMenuSurface(target.Get(),m,2);CheckWin32(SUCCEEDED(target->EndDraw()),"Preview render");
        bitmap->CopyPixels(nullptr,frame.Width()*4,UINT(frame.pixels.size()*4),reinterpret_cast<BYTE*>(frame.pixels.data()));SavePng(frame,L"pin-menu-preview.png");expect(true,"production menu renderer light and dark preview");
    }catch(const std::exception& e){std::cout<<e.what();++failures;}
    CoUninitialize();return failures?1:0;
}

