#include "capture/elements_protocol.h"
#include <ole2.h>
#include <dwmapi.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdlib>
#include <vector>
using Microsoft::WRL::ComPtr;
using namespace lumashot;
namespace {
struct Scan {
    elements::Shared& shared;
    size_t index{};
    ULONGLONG deadline{GetTickCount64()+520};
    bool Full() const {return shared.published>=static_cast<LONG>(elements::MaxRegions)||GetTickCount64()>=deadline;}
    void Add(RECT rect) {
        RECT clipped{};
        if(Full()||!IntersectRect(&clipped,&rect,&shared.windows[index].bounds)||clipped.right-clipped.left<6||clipped.bottom-clipped.top<6)return;
        const LONG count=shared.published;
        for(LONG i=0;i<count;++i)if(shared.regions[i].window_index==index&&EqualRect(&shared.regions[i].bounds,&clipped))return;
        shared.regions[count]={index,clipped};
        InterlockedExchange(&shared.published,count+1);
    }
};
BOOL CALLBACK Child(HWND window,LPARAM param) {
    auto& scan=*reinterpret_cast<Scan*>(param);
    if(scan.Full())return FALSE;
    RECT rect{};
    if(IsWindowVisible(window)&&GetWindowRect(window,&rect)) {
        for(HWND parent=GetParent(window);parent&&parent!=scan.shared.windows[scan.index].window;parent=GetParent(parent)) {
            RECT bounds{};if(!GetWindowRect(parent,&bounds)||!IntersectRect(&rect,&rect,&bounds))return TRUE;
        }
        scan.Add(rect);
    }
    return TRUE;
}
bool Covered(const elements::Shared& shared,size_t index) {
    RECT current{};
    if(!IsWindowVisible(shared.windows[index].window))return true;
    if(FAILED(DwmGetWindowAttribute(shared.windows[index].window,DWMWA_EXTENDED_FRAME_BOUNDS,&current,sizeof(current)))&&!GetWindowRect(shared.windows[index].window,&current))return true;
    if(!SameElementGeometry(shared.windows[index].bounds,current))return true;
    const RECT& bounds=shared.windows[index].bounds;
    for(size_t i=0;i<index;++i) {
        RECT clipped{};
        if(IntersectRect(&clipped,&bounds,&shared.windows[i].bounds)&&EqualRect(&clipped,&bounds))return true;
    }
    return false;
}
void Walk(IUIAutomationElement* node,IUIAutomationTreeWalker* walker,IUIAutomationCacheRequest* cache,Scan& scan,unsigned depth) {
    if(depth>24||scan.Full())return;
    ComPtr<IUIAutomationElement> child;
    if(FAILED(walker->GetFirstChildElementBuildCache(node,cache,&child)))return;
    while(child&&!scan.Full()) {
        BOOL offscreen=TRUE;RECT bounds{};CONTROLTYPEID type{};
        if(SUCCEEDED(child->get_CachedIsOffscreen(&offscreen))&&!offscreen&&SUCCEEDED(child->get_CachedControlType(&type))&&
            type!=UIA_TextControlTypeId&&SUCCEEDED(child->get_CachedBoundingRectangle(&bounds)))scan.Add(bounds);
        Walk(child.Get(),walker,cache,scan,depth+1);
        ComPtr<IUIAutomationElement> next;
        if(FAILED(walker->GetNextSiblingElementBuildCache(child.Get(),cache,&next)))break;
        child=std::move(next);
    }
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 1;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    elements::Handle mapping(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(_wcstoui64(argv[1],nullptr,10))));
    elements::View view(mapping.value);if(!view.value)return 2;
    auto& shared=*view.value;
    if(shared.window_count>elements::MaxWindows)return 3;
    Scan scan{shared};
    std::vector<size_t> order;
    for(size_t i=0;i<shared.window_count;++i)if(!Covered(shared,i))order.push_back(i);
    auto pointer=std::find_if(order.begin(),order.end(),[&](size_t i){return PtInRect(&shared.windows[i].bounds,shared.pointer)!=FALSE;});
    if(pointer!=order.end())std::rotate(order.begin(),pointer,pointer+1);
    // Publish native child bounds before entering any third-party automation provider.
    for(const auto i:order) {scan.index=i;EnumChildWindows(shared.windows[i].window,Child,reinterpret_cast<LPARAM>(&scan));if(scan.Full())break;}
    if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return 0;
    struct ComCleanup {~ComCleanup(){CoUninitialize();}} cleanup;
    ComPtr<IUIAutomation> automation;
    if(FAILED(CoCreateInstance(CLSID_CUIAutomation8,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation))))return 0;
    ComPtr<IUIAutomation2> automation2;
    if(SUCCEEDED(automation.As(&automation2))) {automation2->put_ConnectionTimeout(80);automation2->put_TransactionTimeout(80);}
    ComPtr<IUIAutomationCacheRequest> cache;
    ComPtr<IUIAutomationTreeWalker> walker;
    if(FAILED(automation->CreateCacheRequest(&cache))||FAILED(automation->get_ControlViewWalker(&walker)))return 0;
    cache->put_TreeScope(TreeScope_Element);
    cache->AddProperty(UIA_BoundingRectanglePropertyId);
    cache->AddProperty(UIA_IsOffscreenPropertyId);
    cache->AddProperty(UIA_ControlTypePropertyId);
    unsigned roots{};
    for(const auto i:order) {
        if(scan.Full()||roots++>=8)break;
        scan.index=i;ComPtr<IUIAutomationElement> root;
        if(SUCCEEDED(automation->ElementFromHandle(shared.windows[i].window,&root))&&root)Walk(root.Get(),walker.Get(),cache.Get(),scan,0);
    }
    return 0;
}


