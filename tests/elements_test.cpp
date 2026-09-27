#include "capture/elements.h"
#include <cstdio>
#include <chrono>
#include <thread>
#include <cstring>
using namespace lumashot;
namespace {
bool Same(std::optional<RECT> actual,RECT expected) {return actual&&EqualRect(&*actual,&expected);}
}
#include <ole2.h>
#include <UIAutomation.h>
#include <atomic>
#include <future>

namespace {
struct FixtureProvider final : IRawElementProviderSimple, IRawElementProviderFragment, IRawElementProviderFragmentRoot {
    std::atomic<ULONG> refs{1};
    HWND window{};
    FixtureProvider* parent{};
    FixtureProvider* child{};
    std::atomic<bool> slow{false};
    std::atomic<unsigned> slow_calls{0};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        *out=nullptr;
        if(iid==__uuidof(IUnknown)||iid==__uuidof(IRawElementProviderSimple))*out=static_cast<IRawElementProviderSimple*>(this);
        else if(iid==__uuidof(IRawElementProviderFragment))*out=static_cast<IRawElementProviderFragment*>(this);
        else if(iid==__uuidof(IRawElementProviderFragmentRoot)&&!parent)*out=static_cast<IRawElementProviderFragmentRoot*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override {return --refs;}
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* out) override {*out=ProviderOptions_ServerSideProvider;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID,IUnknown** out) override {*out=nullptr;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id,VARIANT* out) override {
        VariantInit(out);
        if(id==UIA_ControlTypePropertyId) {out->vt=VT_I4;out->lVal=parent?UIA_ButtonControlTypeId:UIA_PaneControlTypeId;}
        else if(id==UIA_IsControlElementPropertyId||id==UIA_IsContentElementPropertyId||id==UIA_IsEnabledPropertyId) {out->vt=VT_BOOL;out->boolVal=VARIANT_TRUE;}
        else if(id==UIA_IsOffscreenPropertyId) {out->vt=VT_BOOL;out->boolVal=VARIANT_FALSE;}
        else if(id==UIA_BoundingRectanglePropertyId) {
            UiaRect bounds{};get_BoundingRectangle(&bounds);
            double values[]{bounds.left,bounds.top,bounds.width,bounds.height};
            out->vt=VT_ARRAY|VT_R8;out->parray=SafeArrayCreateVector(VT_R8,0,4);
            if(!out->parray)return E_OUTOFMEMORY;
            for(LONG i=0;i<4;++i)SafeArrayPutElement(out->parray,&i,&values[i]);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override {
        *out=nullptr;return parent?S_OK:UiaHostProviderFromHwnd(window,out);
    }
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,IRawElementProviderFragment** out) override {
        *out=nullptr;
        if(direction==NavigateDirection_Parent&&parent)*out=parent;
        else if((direction==NavigateDirection_FirstChild||direction==NavigateDirection_LastChild)&&child)*out=child;
        if(*out)(*out)->AddRef();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override {
        *out=nullptr;if(!parent)return S_OK;
        *out=SafeArrayCreateVector(VT_I4,0,2);if(!*out)return E_OUTOFMEMORY;
        LONG a=0,b=1;int append=UiaAppendRuntimeId,id=42;
        SafeArrayPutElement(*out,&a,&append);SafeArrayPutElement(*out,&b,&id);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override {
        if(slow){++slow_calls;Sleep(1800);}
        RECT r{};GetWindowRect(window,&r);
        if(parent)r={r.left+25,r.top+25,r.left+125,r.top+65};
        *out={double(r.left),double(r.top),double(r.right-r.left),double(r.bottom-r.top)};return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override {*out=nullptr;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetFocus() override {return S_OK;}
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {*out=parent?parent:this;(*out)->AddRef();return S_OK;}
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double,double,IRawElementProviderFragment** out) override {*out=child?child:this;(*out)->AddRef();return S_OK;}
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** out) override {*out=nullptr;return S_OK;}
};
FixtureProvider fixture_root,fixture_child;
LRESULT CALLBACK FixtureProc(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_GETOBJECT&&static_cast<LONG>(l)==UiaRootObjectId)return UiaReturnRawElementProvider(window,w,l,&fixture_root);
    if(message==WM_CLOSE){DestroyWindow(window);return 0;}
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,w,l);
}
int AutomationFixture() {
    std::promise<HWND> ready;auto future=ready.get_future();
    std::thread ui([&] {
        CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        WNDCLASSW wc{};wc.lpfnWndProc=FixtureProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"LumaShotSyntheticProvider";RegisterClassW(&wc);
        HWND window=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"",WS_POPUP|WS_VISIBLE,0,0,400,240,nullptr,nullptr,wc.hInstance,nullptr);
        fixture_root.window=window;fixture_root.child=&fixture_child;fixture_child.window=window;fixture_child.parent=&fixture_root;
        CreateWindowExW(0,L"BUTTON",L"",WS_CHILD|WS_VISIBLE,200,140,100,40,window,nullptr,nullptr,nullptr);
        ready.set_value(window);
        MSG m{};while(GetMessageW(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}
        CoUninitialize();
    });
    HWND window=future.get();RECT bounds{};GetWindowRect(window,&bounds);
    ElementScanner scanner;
    auto scan=[&](uint64_t generation) {
        scanner.Start(nullptr,WM_APP,generation,{{window,bounds}},{30,30});
        std::optional<ElementScanResult> result;
        const auto start=GetTickCount64();
        while(!result&&GetTickCount64()-start<1500) {result=scanner.Take();Sleep(5);}
        return result;
    };
    int failure=0;
    auto result=scan(20);RECT expected{25,25,125,65};
    bool found=false;
    if(result)for(const auto& region:result->regions)found|=EqualRect(&expected,&region.bounds)!=FALSE;
    if(!found)failure=9;
    // Fullscreen/kiosk windows (e.g. Chromium) nudge their frame by a pixel once the
    // topmost capture overlay covers them. A one-pixel difference from the frozen rect
    // must keep the window's element regions; a real move must still discard them.
    auto scan_frozen=[&](uint64_t generation,RECT frozen) {
        scanner.Start(nullptr,WM_APP,generation,{{window,frozen}},{30,30});
        std::optional<ElementScanResult> out;
        const auto begin=GetTickCount64();
        while(!out&&GetTickCount64()-begin<1500) {out=scanner.Take();Sleep(5);}
        return out;
    };
    if(!failure) {
        auto jitter=scan_frozen(23,{bounds.left,bounds.top,bounds.right-1,bounds.bottom-1});
        bool kept=false;
        if(jitter)for(const auto& region:jitter->regions)kept|=EqualRect(&expected,&region.bounds)!=FALSE;
        if(!kept)failure=12;
    }
    if(!failure) {
        auto moved=scan_frozen(24,{bounds.left+30,bounds.top+30,bounds.right+30,bounds.bottom+30});
        if(!moved||!moved->regions.empty())failure=13;
    }
    fixture_child.slow=true;
    const auto start=GetTickCount64();result=scan(21);
    if(!result||GetTickCount64()-start>1100||result->regions.empty()||fixture_child.slow_calls==0)failure=10;
    scanner.Start(nullptr,WM_APP,22,{{window,bounds}},{30,30});Sleep(20);scanner.Cancel();
    Sleep(700);if(scanner.Take())failure=11;
    PostMessageW(window,WM_CLOSE,0,0);ui.join();
    return failure;
}
}

int main(int argc,char** argv) {
    const bool missing_helper=argc==2&&std::strcmp(argv[1],"--missing-helper")==0;
    const ElementWindow windows[]={{nullptr,{-200,-100,200,200}},{nullptr,{-300,-200,300,300}}};
    const ElementRegion regions[]={{0,{-220,-110,-100,0}},{0,{-170,-80,-140,-40}},{0,{-160,-70,-158,-68}},{1,{-190,-90,-180,-80}}};
    if(!Same(HitTestElements({-150,-60},windows,regions),{-170,-80,-140,-40}))return 1;
    if(!Same(HitTestElements({-195,-95},windows,regions),{-200,-100,-100,0}))return 2;
    if(HitTestElements({100,100},windows,regions))return 3;
    if(!SameElementGeometry({0,0,3840,2160},{0,0,3840,2160})||!SameElementGeometry({0,0,3840,2160},{0,0,3839,2159})||
       !SameElementGeometry({-8,-8,1928,1088},{-6,-6,1926,1086}))return 14;
    if(SameElementGeometry({0,0,3840,2160},{0,0,3837,2160})||SameElementGeometry({0,0,800,600},{40,0,840,600}))return 15;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HWND root=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Synthetic element test",WS_POPUP|WS_VISIBLE,0,0,420,280,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!root)return 4;
    HWND panel=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE,20,20,300,180,root,nullptr,nullptr,nullptr);
    HWND button=CreateWindowExW(0,L"BUTTON",L"",WS_CHILD|WS_VISIBLE,20,20,90,30,panel,nullptr,nullptr,nullptr);
    HWND edit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE,20,70,180,25,panel,nullptr,nullptr,nullptr);
    HWND hidden=CreateWindowExW(0,L"BUTTON",L"",WS_CHILD,200,130,30,30,panel,nullptr,nullptr,nullptr);
    RECT bounds{},button_bounds{},edit_bounds{},hidden_bounds{};
    GetWindowRect(root,&bounds);GetWindowRect(button,&button_bounds);GetWindowRect(edit,&edit_bounds);GetWindowRect(hidden,&hidden_bounds);
    ElementScanner scanner;
    const auto start=GetTickCount64();
    scanner.Start(nullptr,WM_APP,10,{{root,bounds}},{50,50});
    std::optional<ElementScanResult> result;
    while(GetTickCount64()-start<2500&&!result) {
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        result=scanner.Take();std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    int failure=0;
    if(!result||result->generation!=10)failure=5;
    else {
        bool found_button=false,found_edit=false,found_hidden=false;
        for(const auto& region:result->regions) {
            found_button|=EqualRect(&region.bounds,&button_bounds)!=FALSE;
            found_edit|=EqualRect(&region.bounds,&edit_bounds)!=FALSE;
            found_hidden|=EqualRect(&region.bounds,&hidden_bounds)!=FALSE;
        }
        if(missing_helper) {if(!result->regions.empty())failure=6;}
        else if(!found_button||!found_edit||found_hidden)failure=6;
    }
    scanner.Start(nullptr,WM_APP,11,{{root,bounds}},{50,50});
    const auto cancel_start=GetTickCount64();scanner.Cancel();
    if(GetTickCount64()-cancel_start>50)failure=7;
    std::this_thread::sleep_for(std::chrono::milliseconds(750));
    if(scanner.Take())failure=8;
    DestroyWindow(root);
    if(!failure&&!missing_helper)failure=AutomationFixture();
    std::printf("elements test: %s (%d)\n",failure?"FAIL":"PASS",failure);
    return failure;
}




