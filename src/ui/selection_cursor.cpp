#include "ui/selection_cursor.h"
#include "ui/resize_cursor.h"
#include "ui/render.h"
#include "model/selection.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
namespace lumashot::ui {
namespace {
HCURSOR CreateRotationCursor(int size){
    // Authored at build time: standard 32-DIP canvas, compact 24-DIP artwork.
    return static_cast<HCURSOR>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(4201),
        IMAGE_CURSOR,size,size,0));
}
struct CursorCache {
    std::mutex mutex;std::map<int,HCURSOR> handles;
    ~CursorCache(){for(const auto& [size,cursor]:handles){(void)size;DestroyCursor(cursor);}}
};
}
HCURSOR RotationCursor(UINT dpi){
    if(!dpi)dpi=96;
    static CursorCache cache;
    const int size=std::clamp(MulDiv(32,static_cast<int>(std::clamp(dpi,72u,384u)),96),24,128);
    const std::lock_guard lock(cache.mutex);
    if(const auto it=cache.handles.find(size);it!=cache.handles.end())return it->second;
    if(const auto cursor=CreateRotationCursor(size)){cache.handles.emplace(size,cursor);return cursor;}
    return LoadCursorW(nullptr,IDC_CROSS);
}
HCURSOR SelectionEditCursor(const Document& document,const ViewState& state,Point point,UINT dpi,int active_handle){
    if(!state.selected||state.tool!=Tool::Select||state.busy||state.picker.open||state.dropdown.Open()||
        document.selected<0||static_cast<size_t>(document.selected)>=document.marks.size())return nullptr;
    if(active_handle==8)return RotationCursor(dpi);
    if(active_handle<0&&Contains(state.toolbar.bounds,point))return nullptr;
    const auto& mark=document.marks[document.selected];
    const int handle=active_handle>=0?active_handle:HitEditHandle(mark,document.selected_part,point,state.toolbar.scale);
    if(handle==8)return RotationCursor(dpi);
    if(handle>=9)return LoadCursorW(nullptr,IDC_CROSS);
    if(handle>=0)return ExactResizeHandleCursor(handle,mark.rotation,dpi);
    return HitMark(mark,point)?LoadCursorW(nullptr,IDC_SIZEALL):nullptr;
}
}
