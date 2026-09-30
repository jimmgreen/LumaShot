#include "pin/pin.h"
#include "export/clipboard.h"
#include "export/png.h"
#include "pin/selection_tools.h"
#include "pin/paper.h"
#include "pin/zoom.h"
#include "ui/pin_menu.h"
#include "ui/themed_message.h"
#include "ui/memory_target.h"
#include "ocr/availability.h"
#include "pin/translation.h"
#include "pin/translation_panel.h"
#include "translate/service.h"
#include <windowsx.h>
#include <commdlg.h>
#include <algorithm>
#include <cmath>
namespace lumashot {
struct PinManager::Pin {
    PinManager* manager{};HWND window{};uint64_t id{},version{},revision{1};
    std::shared_ptr<const Frame> image,ocr_image,annotation_base;Document annotations;
    ocr::Text text;ocr::Selection selection;
    std::vector<SelectionMark> marks;
    std::vector<std::vector<SelectionMark>> undo;
    HWND tools{};ComPtr<ID2D1HwndRenderTarget> tools_target;SelectionBar bar;
    bool copied{};int tools_hover{-1},tools_down{-1},handle{-1};Point handle_offset{};
    std::wstring status;bool ocr_enabled{};bool selecting{},moving{},saving{},recognizing{},blank_click{},dragged{};
    POINT drag{},origin{},last_click{};DWORD click_time{};int clicks{};
    bool locked{},editing{};
    float dpi{96};
    float zoom{1},zoom_from{1},zoom_goal{1};POINT zoom_anchor{};Point zoom_image{};
    ULONGLONG zoom_started{},zoom_frame_time{},zoom_badge_until{};bool zoom_animating{};
    std::unique_ptr<DibSurface> surface;int surface_width{},surface_height{};
    PinStyle style{DefaultPinStyle};
    struct Translation {
        TranslationView::State state{TranslationView::State::Idle};uint64_t job{};
        translate::Language requested{translate::Language::Auto},source{translate::Language::Auto},target{translate::Language::ChineseSimplified};
        std::vector<translate::Block> blocks;std::vector<std::wstring> results;std::wstring error,engine;
        std::shared_ptr<const Frame> image;bool show{};std::unique_ptr<TranslationPanel> panel;
    } tr;
    // The frame the pin currently shows, copies and saves: the translated
    // rendering while it is toggled on, otherwise the original image.
    std::shared_ptr<const Frame> Shown()const{return tr.show&&tr.image?tr.image:image;}
    PaperLayout paper,shadow_layout;Frame shadow_image;ComPtr<ID2D1Bitmap> shadow_bitmap;unsigned shadow_builds{};
    Point offset{};POINT destination{};bool relocating{},presenting{};
    int Inset()const{return paper.inset+paper.shadow;}
    int CanvasWidth()const{return paper.width+2*paper.shadow+2;}
    int CanvasHeight()const{return paper.height+2*paper.shadow+2;}
    Point WindowPoint(Point q)const{return {q.x*zoom+Inset()+offset.x,q.y*zoom+Inset()+offset.y};}
    ComPtr<ID2D1Factory> factory;ComPtr<ID2D1RenderTarget> target;ComPtr<ID2D1Bitmap> bitmap;int bitmap_uploads{};TextRenderer text_renderer;
    Point ImagePoint(LPARAM lp) const{return {(GET_X_LPARAM(lp)-Inset()-offset.x)/zoom,(GET_Y_LPARAM(lp)-Inset()-offset.y)/zoom};}
    SelectionHandles Handles()const{
        auto handles=TextHandles(text,selection,dpi/96/zoom);if(!handles.visible)return handles;
        const float margin=std::min({6*dpi/96,float(image->Width())/2,float(image->Height())/2});
        for(auto* p:{&handles.start,&handles.end}){p->x=std::clamp(p->x,margin,float(image->Width())-margin);p->y=std::clamp(p->y,margin,float(image->Height())-margin);}return handles;
    }
    bool Blank(Point point) const {
        if(point.x<0||point.y<0||point.x>=image->Width()||point.y>=image->Height())return true;
        if(ocr::OnText(text,point))return false;
        if(!recognizing)return true;
        // Until OCR finishes, only close on a locally uniform patch. Text must
        // not become a close target just because its recognition is pending.
        const int x=std::clamp(int(point.x),0,image->Width()-1),y=std::clamp(int(point.y),0,image->Height()-1);
        const auto reference=image->pixels[static_cast<size_t>(y)*image->Width()+x];
        for(int yy=std::max(0,y-12);yy<std::min(image->Height(),y+13);++yy)
            for(int xx=std::max(0,x-12);xx<std::min(image->Width(),x+13);++xx) {
                const auto pixel=image->pixels[static_cast<size_t>(yy)*image->Width()+xx];
                for(int shift:{0,8,16})if(std::abs(int((pixel>>shift)&255)-int((reference>>shift)&255))>12)return false;
            }
        return true;
    }
};
PinManager::PinManager(HWND host):host_(host),service_(host,kOcrReady) {
    WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"LumaShot.Pin";
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.style=CS_DBLCLKS;
    if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)CheckWin32(false,"Register pin window");
    wc.lpfnWndProc=ToolsProc;wc.lpszClassName=L"LumaShot.SelectionTools";wc.style=CS_DROPSHADOW;
    if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)CheckWin32(false,"Register selection tools");
}
PinManager::~PinManager(){try{PreserveSession();}catch(...){}session_writer_.reset();for(auto& [id,pin]:pins_){service_.Cancel(id);if(pin->window)DestroyWindow(pin->window);}pins_.clear();}
void PinManager::Create(Frame image,Frame ocr_image,POINT position,std::optional<Frame> base,Document annotations,bool recognize,const PinSessionRecord* restored) {
    auto pin=std::make_unique<Pin>();pin->manager=this;pin->id=next_id_++;
    if(!restored){image.bounds={0,0,image.Width(),image.Height()};ocr_image.bounds=image.bounds;}
    // Identical (or empty) OCR pixels share the image instead of a second copy;
    // for a long capture that is one full image less per pin.
    const bool same_ocr=!restored&&(ocr_image.pixels.empty()||ocr_image.pixels==image.pixels);
    pin->image=restored?restored->image:std::make_shared<Frame>(std::move(image));
    pin->ocr_image=restored?restored->ocr_image:same_ocr?pin->image:std::make_shared<Frame>(std::move(ocr_image));
    if(base)base->bounds={0,0,base->Width(),base->Height()};
    pin->annotation_base=base?std::make_shared<Frame>(std::move(*base)):pin->image;pin->annotations=std::move(annotations);
    if(restored){pin->annotation_base=restored->base;pin->annotations.marks=restored->annotations;pin->marks=restored->decorations;pin->locked=restored->locked;}
    pin->style=restored?restored->style:sticker_style?NormalizePinStyle(static_cast<int>(sticker_style())):DefaultPinStyle;
    pin->paper=MakePaperLayout(pin->image->Width(),pin->image->Height(),96,pin->style);
    const uint64_t id=pin->id;auto* p=pin.get();pins_.emplace(id,std::move(pin));last_created_=id;
    p->window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOPMOST|WS_EX_TOOLWINDOW,L"LumaShot.Pin",L"LumaShot 贴图",WS_POPUP,
        position.x-p->Inset(),position.y-p->Inset(),p->CanvasWidth(),p->CanvasHeight(),nullptr,nullptr,GetModuleHandleW(nullptr),p);
    if(!p->window){pins_.erase(id);CheckWin32(false,"Create pinned image");}
    p->dpi=float(GetDpiForWindow(p->window));
    if(restored)p->zoom=p->zoom_from=p->zoom_goal=PinZoomGoal(restored->zoom,0,p->image->Width(),p->image->Height(),p->dpi,p->style);
    else{
        // Tall images (long captures) start fitted to 90% of the work area instead of spilling off screen.
        MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromPoint(position,MONITOR_DEFAULTTONEAREST),&monitor);
        const int work=monitor.rcWork.bottom-monitor.rcWork.top;
        if(work>0&&p->image->Height()>work)p->zoom=p->zoom_from=p->zoom_goal=PinZoomGoal(std::min(1.f,float(work)*.9f/float(p->image->Height())),0,p->image->Width(),p->image->Height(),p->dpi,p->style);
    }
    const auto paper=MakePaperLayout(std::max(1,int(std::lround(p->image->Width()*p->zoom))),std::max(1,int(std::lround(p->image->Height()*p->zoom))),p->dpi,p->style);
    ApplyPaper(*p,{float(position.x-paper.inset-paper.shadow),float(position.y-paper.inset-paper.shadow)});
    if(restored){
        MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromPoint(position,MONITOR_DEFAULTTONEAREST),&monitor);
        const float left=std::clamp(restored->origin.x-p->Inset(),float(monitor.rcWork.left),float(std::max(monitor.rcWork.left,monitor.rcWork.right-p->CanvasWidth())));
        const float top=std::clamp(restored->origin.y-p->Inset(),float(monitor.rcWork.top),float(std::max(monitor.rcWork.top,monitor.rcWork.bottom-p->CanvasHeight())));
        ApplyPaper(*p,{left,top});
    }
    ShowWindow(p->window,SW_SHOWNOACTIVATE);
    if(!restoring_session_)SetForegroundWindow(p->window);if(recognize)Recognize(*p);SessionChanged();
}
std::vector<PinSessionRecord> PinManager::SessionSnapshot()const{
    std::vector<PinSessionRecord> result;
    for(const auto& [id,p]:pins_){if(!p->window||!p->image)continue;RECT rect{};GetWindowRect(p->window,&rect);
        PinSessionRecord record;record.id=id;record.revision=p->revision;record.origin={rect.left+p->Inset()+p->offset.x,rect.top+p->Inset()+p->offset.y};record.zoom=p->zoom;record.style=p->style;record.locked=p->locked;record.recognize=p->ocr_enabled;
        record.image=p->image;record.ocr_image=p->ocr_image;record.base=p->annotation_base;record.annotations=p->annotations.marks;record.decorations=p->marks;result.push_back(std::move(record));
    }return result;
}
void PinManager::SessionChanged(){if(session_writer_&&!restoring_session_&&!preserving_session_)session_writer_->Request(SessionSnapshot());}
bool PinManager::FlushSession(){if(preserving_session_)return preserve_ok_;if(!session_writer_||restoring_session_)return true;session_writer_->Request(SessionSnapshot());return session_writer_->Flush();}
void PinManager::PreserveSession(){if(!preserving_session_){preserve_ok_=FlushSession();preserving_session_=true;}}
bool PinManager::EnableSession(const std::filesystem::path& directory){
    if(directory.empty()||session_store_)return false;
    try{
        session_store_=std::make_unique<PinSessionStore>(directory);
        // PNG decoding is performed on a worker, before any restored windows are created.
        auto load=std::async(std::launch::async,[this]{const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(com))throw std::runtime_error("Session COM initialization");struct End{~End(){CoUninitialize();}}end;unsigned skipped{};auto records=session_store_->Load(skipped);return std::pair{std::move(records),skipped};});
        auto [records,skipped]=load.get();
        session_writer_=std::make_unique<DeferredWriter<std::vector<PinSessionRecord>>>([this](const auto& state){try{session_store_->Save(state);session_warning_=false;}catch(...){if(!session_warning_.exchange(true))PostMessageW(host_,kPinSessionError,0,0);throw;}},[]{CoInitializeEx(nullptr,COINIT_MULTITHREADED);},[]{CoUninitialize();});
        restoring_session_=true;
        for(const auto& record:records){try{Create({}, {},{LONG(std::lround(record.origin.x)),LONG(std::lround(record.origin.y))},std::nullopt,{},record.recognize,&record);}catch(...){++skipped;}}
        restoring_session_=false;return skipped==0;
    }catch(...){restoring_session_=false;session_writer_.reset();session_store_.reset();return false;}
}
void PinManager::Recognize(Pin& p){if(!ocr::Available())return;p.ocr_enabled=true;++p.version;p.text={};p.selection={};UpdateTools(p);p.recognizing=true;p.status=L"正在识别文字…";service_.Submit(p.id,p.version,p.ocr_image);InvalidateRect(p.window,nullptr,FALSE);}
void PinManager::Ready() {
    for(auto& result:service_.Take()) {
        const auto it=pins_.find(result.id);if(it==pins_.end()||it->second->version!=result.version||!it->second->window)continue;
        auto& p=*it->second;p.text=std::move(result.text);p.recognizing=false;
        p.status=!result.error.empty()?L"识别失败 · 右键可重试":(p.text.lines.empty()?L"未识别到文字":L"");
        if(p.tr.state==TranslationView::State::WaitingOcr){
            if(!result.error.empty()){p.tr.state=TranslationView::State::Failed;p.tr.error=L"文字识别失败："+result.error;UpdatePanel(p);}
            else StartTranslation(p);
        }
        InvalidateRect(p.window,nullptr,FALSE);
    }
    std::erase_if(pins_,[](const auto& item){return !item.second->window;});
}
void PinManager::ApplyPaper(Pin& p,Point origin){
    const auto layout=MakePaperLayout(std::max(1,int(std::lround(p.image->Width()*p.zoom))),std::max(1,int(std::lround(p.image->Height()*p.zoom))),p.dpi,p.style);
    const auto shadow_layout=PaperShadowPatchLayout(layout);
    if(!(shadow_layout==p.shadow_layout)||(layout.shadow&&p.shadow_image.pixels.empty())){
        p.shadow_layout=shadow_layout;p.shadow_image=PaperShadowImage(shadow_layout,true);p.shadow_bitmap.Reset();++p.shadow_builds;
    }
    p.paper=layout;p.destination={LONG(std::floor(origin.x)),LONG(std::floor(origin.y))};
    p.offset={origin.x-p.destination.x,origin.y-p.destination.y};p.relocating=true;
    Paint(p);UpdateTools(p);PlacePanel(p);
}
void PinManager::RefreshAppearance(){
    const auto style=sticker_style?NormalizePinStyle(static_cast<int>(sticker_style())):DefaultPinStyle;
    for(auto& [id,pin]:pins_){
        auto& p=*pin;if(!p.window||p.style==style)continue;
        RECT bounds{};GetWindowRect(p.window,&bounds);
        const Point image_origin{bounds.left+p.Inset()+p.offset.x,bounds.top+p.Inset()+p.offset.y};
        KillTimer(p.window,101);p.zoom_animating=false;p.zoom_goal=p.zoom;p.style=style;
        const auto layout=MakePaperLayout(std::max(1,int(std::lround(p.image->Width()*p.zoom))),std::max(1,int(std::lround(p.image->Height()*p.zoom))),p.dpi,p.style);
        ApplyPaper(p,{image_origin.x-layout.inset-layout.shadow,image_origin.y-layout.inset-layout.shadow});
    }SessionChanged();
}
void PinManager::Paint(Pin& p) {
    if(!p.factory)CheckWin32(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,p.factory.GetAddressOf())),"Create pin renderer");
    if(!p.surface||!p.target||p.surface_width!=p.CanvasWidth()||p.surface_height!=p.CanvasHeight()){
        // D2D draws in place into the layered window's DIB (no per-frame GDI round trip).
        // Zoom resizes the canvas every frame; the image and shadow move to the new
        // target as shared software bitmaps instead of being uploaded again.
        auto surface=std::make_unique<DibSurface>(p.CanvasWidth(),p.CanvasHeight());
        auto target=CreateMemoryRenderTarget(p.factory.Get(),surface->Pixels(),p.CanvasWidth(),p.CanvasHeight(),p.CanvasWidth());
        ShareBitmap(target.Get(),p.bitmap);ShareBitmap(target.Get(),p.shadow_bitmap);p.target=std::move(target);
        p.surface=std::move(surface);p.surface_width=p.CanvasWidth();p.surface_height=p.CanvasHeight();
    }
    if(!p.bitmap){
        const auto bmp=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
        const auto shown=p.Shown();CheckWin32(SUCCEEDED(p.target->CreateBitmap(D2D1::SizeU(shown->Width(),shown->Height()),shown->pixels.data(),shown->Width()*4,bmp,&p.bitmap)),"Create pin image");++p.bitmap_uploads;
    }
    Draw(p,p.target.Get());RECT rect{};GetWindowRect(p.window,&rect);POINT origin=p.relocating?p.destination:POINT{rect.left,rect.top},source{};SIZE size{p.CanvasWidth(),p.CanvasHeight()};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    p.presenting=true;const BOOL ok=UpdateLayeredWindow(p.window,nullptr,&origin,&size,p.surface->Dc(),&source,0,&blend,ULW_ALPHA);p.presenting=false;p.relocating=false;
    CheckWin32(ok!=FALSE,"Present paper and shadow atomically");ValidateRect(p.window,nullptr);
}
void PinManager::Zoom(Pin& p,int delta,POINT anchor){
    if(!delta||p.moving||p.selecting||p.locked||p.editing)return;
    POINT local=anchor;ScreenToClient(p.window,&local);p.zoom_image={(local.x-p.Inset()-p.offset.x)/p.zoom,(local.y-p.Inset()-p.offset.y)/p.zoom};p.zoom_anchor=anchor;
    p.zoom_from=p.zoom;
    p.zoom_goal=PinZoomGoal(p.zoom_goal,delta,p.image->Width(),p.image->Height(),p.dpi,p.style);
    const auto now=GetTickCount64();
    if(!p.zoom_animating)p.zoom_frame_time=now;
    // Restart easing from the last displayed frame, not the last input event:
    // wheel messages arriving immediately before a timer must not reset t to zero.
    p.zoom_started=p.zoom_frame_time;p.zoom_badge_until=now+1100;
    // Retarget without postponing the next tick during high-rate wheel input.
    if(!p.zoom_animating)p.zoom_animating=SetTimer(p.window,101,16,nullptr)!=0;
    SetTimer(p.window,102,1100,nullptr);
}
void PinManager::Draw(Pin& p,ID2D1RenderTarget* target) {
    ComPtr<ID2D1SolidColorBrush> brush;target->CreateSolidColorBrush(D2D1::ColorF(0x202733),brush.GetAddressOf());
    target->BeginDraw();target->SetTransform(D2D1::Matrix3x2F::Identity());target->Clear(D2D1::ColorF(0,0.f));
    ComPtr<ID2D1Bitmap> shadow;
    if(target==p.target.Get())shadow=p.shadow_bitmap;
    if(!shadow&&!p.shadow_image.pixels.empty()){
        const auto& f=p.shadow_image;CheckWin32(SUCCEEDED(target->CreateBitmap(D2D1::SizeU(f.Width(),f.Height()),f.pixels.data(),f.Width()*4,D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&shadow)),"Cache composite shadow");
        if(target==p.target.Get())p.shadow_bitmap=shadow;
    }
    target->SetTransform(D2D1::Matrix3x2F::Translation(p.offset.x,p.offset.y));if(shadow)DrawPaperShadowPatch(target,shadow.Get(),p.paper);
    target->SetTransform(D2D1::Matrix3x2F::Translation(p.paper.shadow+p.offset.x,p.paper.shadow+p.offset.y));DrawPaper(target,p.paper);
    target->SetTransform(D2D1::Matrix3x2F::Scale(p.zoom,p.zoom)*D2D1::Matrix3x2F::Translation(p.Inset()+p.offset.x,p.Inset()+p.offset.y));
    ComPtr<ID2D1Bitmap> bitmap;if(target==p.target.Get())bitmap=p.bitmap;
    const auto props=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
    if(!bitmap){const auto shown=p.Shown();CheckWin32(SUCCEEDED(target->CreateBitmap(D2D1::SizeU(static_cast<UINT>(shown->Width()),static_cast<UINT>(shown->Height())),shown->pixels.data(),static_cast<UINT>(shown->Width()*4),props,bitmap.GetAddressOf())),"Render pinned image");}
    target->DrawBitmap(bitmap.Get(),D2D1::RectF(0,0,float(p.image->Width()),float(p.image->Height())));
    DrawTextMarks(target,p.marks);
    brush->SetColor(D2D1::ColorF(0x3388ff,0.32f));
    for(auto b:SelectionBoxes(p.text,p.selection))target->FillRectangle(D2D1::RectF(b.left,b.top,b.right,b.bottom),brush.Get());
    DrawTextHandles(target,p.Handles(),p.dpi/96/p.zoom);
    target->SetTransform(D2D1::Matrix3x2F::Identity());
    if(GetTickCount64()<p.zoom_badge_until){
        const float scale=p.dpi/96,w=64*scale,h=26*scale,x=p.paper.shadow+(p.paper.width-w)/2+p.offset.x,y=p.paper.shadow+6*scale+p.offset.y;
        brush->SetColor(D2D1::ColorF(0x243142,.9f));target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x,y,x+w,y+h),h/2,h/2),brush.Get());
        ComPtr<IDWriteFactory> dw;DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(dw.GetAddressOf()));ComPtr<IDWriteTextFormat> format;
        dw->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_SEMI_BOLD,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,13*scale,L"en-US",&format);
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        const auto label=std::to_wstring(int(std::lround(p.zoom*100)))+L"%";p.text_renderer.Draw(target,dw.Get(),label,format.Get(),D2D1::RectF(x,y,x+w,y+h),D2D1::ColorF(0xffffff),false);
    }
    if(target->EndDraw()==D2DERR_RECREATE_TARGET){p.bitmap.Reset();p.shadow_bitmap.Reset();p.target.Reset();InvalidateRect(p.window,nullptr,FALSE);}
}

void PinManager::Save(Pin& p) {
    if(p.saving)return;
    wchar_t filename[32768]=L"LumaShot-pin.png";OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner=p.window;dialog.lpstrFilter=L"PNG 图片 (*.png)\0*.png\0\0";dialog.lpstrFile=filename;dialog.nMaxFile=32768;dialog.lpstrDefExt=L"png";dialog.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(!GetSaveFileNameW(&dialog))return;
    const auto image=p.Shown();const auto marks=p.marks;const auto id=p.id;const HWND host=host_;const std::filesystem::path path=filename;p.saving=true;
    saves_.push_back(std::async(std::launch::async,[image,marks,id,path,host] {
        SaveResult result;result.id=id;const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        std::filesystem::path temp=path;temp+=L".lumashot-pin-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(id)+L".tmp";
        bool reserved=false;
        try {
            CheckWin32(SUCCEEDED(com),"Initialize PNG export");ocr::Handle file(CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
            CheckWin32(file.Get()!=INVALID_HANDLE_VALUE,"Reserve PNG file");reserved=true;file.Reset();SavePng(FlattenTextMarks(*image,marks),temp);
            CheckWin32(MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE,"Save pinned image");
        }catch(const std::exception& e){result.error=ocr::ErrorMessage(e);}
        if(reserved)DeleteFileW(temp.c_str());if(SUCCEEDED(com))CoUninitialize();PostMessageW(host,kPinSaveReady,0,0);return result;
    }));
    SetTimer(host_,kPinSaveReady,100,nullptr);
}
void PinManager::Copy(Pin& p){
    if(p.saving)return;p.saving=true;
    const auto image=p.Shown();const auto marks=p.marks;const auto id=p.id;const auto host=host_;const int format=clipboard_format?clipboard_format():-1;
    saves_.push_back(std::async(std::launch::async,[image,marks,id,host,format]{
        SaveResult result;result.id=id;const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        try{CheckWin32(SUCCEEDED(com),"Initialize clipboard export");Frame flattened;const Frame* source=image.get();if(!marks.empty()){flattened=FlattenTextMarks(*image,marks);source=&flattened;}auto file=format>=0?PrepareClipboardFile(*source,format):nullptr;result.clipboard_image=PrepareClipboardImage(*source,file);}
        catch(const std::exception& e){result.error=ocr::ErrorMessage(e);}
        if(SUCCEEDED(com))CoUninitialize();PostMessageW(host,kPinSaveReady,0,0);return result;
    }));SetTimer(host_,kPinSaveReady,100,nullptr);
}
void PinManager::Saved() {
    // Both completion messages and the timer can arrive in a modal message loop.
    if(processing_saves_)return;
    processing_saves_=true;
    struct Reset{bool& flag;~Reset(){flag=false;}} reset{processing_saves_};
    std::vector<SaveResult> completed;
    for(auto it=saves_.begin();it!=saves_.end();) {
        if(it->wait_for(std::chrono::seconds(0))!=std::future_status::ready){++it;continue;}
        completed.push_back(it->get());it=saves_.erase(it);
    }
    // Never retain a saves_ iterator across clipboard or dialog calls. New jobs
    // may be submitted while the dialog is open; the timer will drain them later.
    for(const auto& result:completed) {
        const auto pin=pins_.find(result.id);
        if(pin==pins_.end()||!pin->second->window)continue;
        const HWND owner=pin->second->window;pin->second->saving=false;
        if(!result.error.empty())ui::ShowThemedMessage(owner,dark_theme&&dark_theme(),L"导出失败",result.error);
        else if(result.clipboard_image)try{PublishClipboardImage(owner,*result.clipboard_image);}catch(const std::exception& e){ui::ShowThemedMessage(IsWindow(owner)?owner:host_,dark_theme&&dark_theme(),L"复制失败",ocr::ErrorMessage(e));}
    }
    if(saves_.empty())KillTimer(host_,kPinSaveReady);
}
void PinManager::Menu(Pin& source,POINT point) {
    const auto id=source.id;
    if(point.x==-1&&point.y==-1){RECT r{};GetWindowRect(source.window,&r);point={r.left+source.Inset()+16,r.top+source.Inset()+16};}
    PinMenuModel model;model.dark=dark_theme?dark_theme():false;model.status=source.status;model.locked=source.locked;model.recognized=source.ocr_enabled;model.table=source.text.table.has_value();
    model.ocr_available=ocr::Available();model.translated=source.tr.state==TranslationView::State::Done;
    model.enabled={bool(annotate)&&!source.editing,true,!source.selection.Empty(),!source.text.lines.empty(),model.table,true,!source.saving,!source.recognizing&&model.ocr_available,model.ocr_available&&!source.editing,true};
    if(source.tools)ShowWindow(source.tools,SW_HIDE);
    const int command=TrackPinMenu(source.window,point,model);
    // OCR notifications may remove a closed pin during the popup's message loop.
    const auto found=pins_.find(id);if(found==pins_.end()||!found->second->window)return;
    auto& p=*found->second;
    if(command==1)CopyText(p.window,ocr::Selected(p.text,p.selection));if(command==2)CopyText(p.window,ocr::Selected(p.text,ocr::All(p.text)));
    if(command==9&&p.text.table)CopyText(p.window,ocr::TableTsv(*p.text.table));
    if(command==3)Copy(p);if(command==4)Save(p);if(command==5)Recognize(p);
    if(command==6){DestroyWindow(p.window);return;}if(command==10)TranslatePin(p);if(command==7)Annotate(p);if(command==8)ToggleLock(p);UpdateTools(p);
}
void PinManager::ToggleLock(Pin& p){
    p.locked=!p.locked;KillTimer(p.window,101);p.zoom_animating=false;p.zoom_goal=p.zoom;p.moving=p.selecting=false;
    if(GetCapture()==p.window)ReleaseCapture();UpdateTools(p);SessionChanged();
}
void PinManager::Annotate(Pin& p){
    if(!annotate||p.editing)return;
    KillTimer(p.window,101);p.zoom_animating=false;p.zoom_goal=p.zoom;p.moving=p.selecting=false;
    if(GetCapture()==p.window)ReleaseCapture();
    POINT origin{};ClientToScreen(p.window,&origin);auto a=p.WindowPoint({0,0}),b=p.WindowPoint({float(p.image->Width()),float(p.image->Height())});
    RECT bounds{origin.x+LONG(std::lround(a.x)),origin.y+LONG(std::lround(a.y)),origin.x+LONG(std::lround(b.x)),origin.y+LONG(std::lround(b.y))};
    p.editing=true;if(p.tools)ShowWindow(p.tools,SW_HIDE);if(p.tr.panel)p.tr.panel->Hide();
    try{
        // Entering annotate must not copy the pin image twice on the UI thread.
        // Without text marks the shared source is forwarded with no copy at all.
        const auto flattened=p.marks.empty()?p.annotation_base:std::make_shared<Frame>(FlattenTextMarks(*p.annotation_base,p.marks));
        if(!annotate(p.id,flattened,p.annotations,bounds))p.editing=false;}catch(...){p.editing=false;throw;}
}
void PinManager::CompleteAnnotation(uint64_t id,std::optional<Frame> image,Document annotations,std::optional<Frame> base,bool recognize){
    const auto found=pins_.find(id);if(found==pins_.end()||!found->second->window)return;auto& p=*found->second;p.editing=false;
    if(base)base->bounds={0,0,base->Width(),base->Height()};
    if(image){++p.revision;image->bounds={0,0,image->Width(),image->Height()};p.image=std::make_shared<Frame>(std::move(*image));p.ocr_image=p.image;p.annotation_base=base?std::make_shared<Frame>(std::move(*base)):p.image;p.annotations=std::move(annotations);p.marks.clear();p.undo.clear();p.bitmap.Reset();p.target.Reset();p.shadow_bitmap.Reset();
        ResetTranslation(p);service_.Cancel(p.id);++p.version;p.text={};p.selection={};p.recognizing=false;p.status.clear();
        if(p.ocr_enabled||recognize)Recognize(p);}
    UpdateTools(p);InvalidateRect(p.window,nullptr,FALSE);SetForegroundWindow(p.window);SessionChanged();
}
void PinManager::UpdateTools(Pin& p){
    const auto boxes=SelectionBoxes(p.text,p.selection);
    if(boxes.empty()||p.selecting||p.moving||p.editing){if(p.tools)ShowWindow(p.tools,SW_HIDE);return;}
    Box selected=boxes.front();for(auto b:boxes){selected.left=std::min(selected.left,b.left);selected.right=std::max(selected.right,b.right);selected.top=std::min(selected.top,b.top);selected.bottom=std::max(selected.bottom,b.bottom);}
    POINT origin{p.Inset(),p.Inset()};ClientToScreen(p.window,&origin);selected.left=selected.left*p.zoom+origin.x+p.offset.x;selected.right=selected.right*p.zoom+origin.x+p.offset.x;selected.top=selected.top*p.zoom+origin.y+p.offset.y;selected.bottom=selected.bottom*p.zoom+origin.y+p.offset.y;
    MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromPoint({LONG((selected.left+selected.right)/2),LONG(selected.bottom)},MONITOR_DEFAULTTONEAREST),&monitor);
    p.bar=PlaceSelectionBar(selected,monitor.rcWork,p.dpi/96);
    if(!p.tools)p.tools=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"LumaShot.SelectionTools",L"选中文字",WS_POPUP,0,0,1,1,p.window,nullptr,GetModuleHandleW(nullptr),&p);
    const auto r=p.bar.bounds;SetWindowPos(p.tools,HWND_TOPMOST,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    const int w=r.right-r.left,h=r.bottom-r.top,n=int(8*p.bar.scale);
    HRGN region=CreateRoundRectRgn(0,p.bar.above?0:n,w+1,p.bar.above?h-n:h+1,int(28*p.bar.scale),int(28*p.bar.scale));
    POINT points[]={{w/2-n,p.bar.above?h-n:n},{w/2,p.bar.above?h:0},{w/2+n,p.bar.above?h-n:n}};HRGN triangle=CreatePolygonRgn(points,3,WINDING);CombineRgn(region,region,triangle,RGN_OR);DeleteObject(triangle);
    if(!SetWindowRgn(p.tools,region,TRUE))DeleteObject(region);
    InvalidateRect(p.tools,nullptr,FALSE);
}
void PinManager::ToolCommand(Pin& p,int command){
    if(p.selection.Empty())return;
    if(command==0){const auto text=ocr::Selected(p.text,p.selection);if(text.empty())return;CopyText(p.window,text);p.copied=true;if(p.tools){SetTimer(p.tools,1,1400,nullptr);InvalidateRect(p.tools,nullptr,FALSE);}return;}
    if(command<1||command>4)return;
    if(p.undo.size()>=50)p.undo.erase(p.undo.begin());p.undo.push_back(p.marks);
    SelectionMark mark{static_cast<TextDecoration>(command-1),SelectionBoxes(p.text,p.selection)};
    const auto same=[&](const SelectionMark& other){if(other.kind!=mark.kind||other.boxes.size()!=mark.boxes.size())return false;for(size_t i=0;i<mark.boxes.size();++i){const auto a=other.boxes[i],b=mark.boxes[i];if(a.left!=b.left||a.top!=b.top||a.right!=b.right||a.bottom!=b.bottom)return false;}return true;};
    auto existing=std::find_if(p.marks.begin(),p.marks.end(),same);if(existing!=p.marks.end())p.marks.erase(existing);else p.marks.push_back(std::move(mark));++p.revision;SessionChanged();
    InvalidateRect(p.window,nullptr,FALSE);
}
LRESULT CALLBACK PinManager::ToolsProc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto* p=reinterpret_cast<Pin*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){p=static_cast<Pin*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));}
    if(!p)return DefWindowProcW(window,message,wp,lp);
    try{switch(message){
    case WM_TIMER:if(wp==1){KillTimer(window,1);p->copied=false;InvalidateRect(window,nullptr,FALSE);}return 0;
    case WM_KEYDOWN:if(wp==VK_ESCAPE&&p->window){SendMessageW(p->window,WM_KEYDOWN,wp,lp);return 0;}break;
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_ERASEBKGND:return 1;
    case WM_SIZE:if(p->tools_target)p->tools_target->Resize(D2D1::SizeU(LOWORD(lp),HIWORD(lp)));return 0;
    case WM_PAINT:{PAINTSTRUCT ps{};BeginPaint(window,&ps);try{RECT r{};GetClientRect(window,&r);
        if(!p->factory)D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,p->factory.GetAddressOf());
        if(!p->tools_target)CheckWin32(SUCCEEDED(p->factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,D2D1::PixelFormat(),96,96),D2D1::HwndRenderTargetProperties(window,D2D1::SizeU(r.right,r.bottom)),&p->tools_target)),"Create selection tools");
        p->tools_target->BeginDraw();p->tools_target->Clear(D2D1::ColorF(0xf4f8ff));DrawSelectionBar(p->tools_target.Get(),float(r.right),float(r.bottom),p->bar.scale,p->tools_hover,p->bar.above,p->copied,p->tools_down);
        if(p->tools_target->EndDraw()==D2DERR_RECREATE_TARGET){p->tools_target.Reset();InvalidateRect(window,nullptr,FALSE);}
        }catch(...){EndPaint(window,&ps);throw;}EndPaint(window,&ps);return 0;}
    case WM_MOUSEMOVE:{RECT r{};GetClientRect(window,&r);const int hover=std::clamp(GET_X_LPARAM(lp)*5/std::max(1L,r.right),0L,4L);if(hover!=p->tools_hover){p->tools_hover=hover;InvalidateRect(window,nullptr,FALSE);}TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0};TrackMouseEvent(&track);return 0;}
    case WM_MOUSELEAVE:p->tools_hover=-1;InvalidateRect(window,nullptr,FALSE);return 0;
    case WM_LBUTTONDOWN:{RECT r{};GetClientRect(window,&r);p->tools_down=std::clamp(GET_X_LPARAM(lp)*5/std::max(1L,r.right),0L,4L);SetCapture(window);InvalidateRect(window,nullptr,FALSE);return 0;}
    case WM_LBUTTONUP:{RECT r{};GetClientRect(window,&r);const POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};const int down=p->tools_down;p->tools_down=-1;ReleaseCapture();if(PtInRect(&r,point)&&down==point.x*5/std::max(1L,r.right))p->manager->ToolCommand(*p,down);return 0;}
    case WM_CAPTURECHANGED:p->tools_down=-1;InvalidateRect(window,nullptr,FALSE);return 0;
    case WM_NCDESTROY:p->tools=nullptr;p->tools_target.Reset();SetWindowLongPtrW(window,GWLP_USERDATA,0);break;
    }}catch(const std::exception& e){ui::ShowThemedMessage(p->window,p->manager->dark_theme&&p->manager->dark_theme(),L"LumaShot",ocr::ErrorMessage(e));}
    return DefWindowProcW(window,message,wp,lp);
}
LRESULT CALLBACK PinManager::Proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto* p=reinterpret_cast<Pin*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){p=static_cast<Pin*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);p->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));}
    if(!p)return DefWindowProcW(window,message,wp,lp);
    try {
        switch(message) {
        case WM_PAINT:{PAINTSTRUCT ps{};BeginPaint(window,&ps);try{p->manager->Paint(*p);}catch(...){EndPaint(window,&ps);throw;}EndPaint(window,&ps);return 0;}
        case WM_ERASEBKGND:return 1;
        case WM_SIZE:if(!p->presenting)InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_NCHITTEST:{POINT local{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(window,&local);HRGN region=PaperRegion(p->paper);const bool inside=PtInRegion(region,int(local.x-p->paper.shadow-p->offset.x),int(local.y-p->paper.shadow-p->offset.y))!=0;DeleteObject(region);return inside?HTCLIENT:HTTRANSPARENT;}
        case WM_MOUSEWHEEL:p->manager->Zoom(*p,GET_WHEEL_DELTA_WPARAM(wp),{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});return 0;
        case WM_TIMER:
            if(wp==101){
                if(!p->zoom_animating)return 0;
                p->zoom_frame_time=GetTickCount64();
                const float t=std::min(1.f,float(p->zoom_frame_time-p->zoom_started)/140.f),ease=1-std::pow(1-t,3.f);
                p->zoom=p->zoom_from+(p->zoom_goal-p->zoom_from)*ease;
                const auto layout=MakePaperLayout(std::max(1,int(std::lround(p->image->Width()*p->zoom))),std::max(1,int(std::lround(p->image->Height()*p->zoom))),p->dpi,p->style);
                p->manager->ApplyPaper(*p,{p->zoom_anchor.x-layout.inset-layout.shadow-p->zoom_image.x*p->zoom,p->zoom_anchor.y-layout.inset-layout.shadow-p->zoom_image.y*p->zoom});
                if(t>=1){KillTimer(window,101);p->zoom_animating=false;p->manager->SessionChanged();}return 0;
            }
            if(wp==103){KillTimer(window,103);p->manager->SessionChanged();return 0;}
            if(wp==102){KillTimer(window,102);p->zoom_badge_until=0;InvalidateRect(window,nullptr,FALSE);return 0;}break;
        case WM_DPICHANGED:{p->dpi=float(HIWORD(wp));const auto* r=reinterpret_cast<RECT*>(lp);p->manager->ApplyPaper(*p,{float(r->left),float(r->top)});return 0;}
        case WM_MOVE:if(!p->presenting)p->manager->PlacePanel(*p);if(!p->presenting&&!p->moving){p->manager->UpdateTools(*p);if(p->manager->session_writer_&&!p->manager->restoring_session_)SetTimer(window,103,180,nullptr);}return 0;
        case WM_SETCURSOR:if(LOWORD(lp)==HTCLIENT){POINT pt{};GetCursorPos(&pt);ScreenToClient(window,&pt);SetCursor(LoadCursorW(nullptr,ocr::OnText(p->text,p->ImagePoint(MAKELPARAM(pt.x,pt.y)))&&!(GetKeyState(VK_SPACE)&0x8000)?IDC_IBEAM:IDC_ARROW));return TRUE;}break;
        case WM_LBUTTONDOWN:case WM_LBUTTONDBLCLK:{
            if(p->editing)return 0;KillTimer(window,101);p->zoom_animating=false;p->zoom_goal=p->zoom;SetFocus(window);
            const auto point=p->ImagePoint(lp);
            const bool space=false;
            p->handle=space?-1:HitTextHandle(p->Handles(),point,p->dpi/96/p->zoom);
            if(p->handle>=0){
                const auto boxes=SelectionBoxes(p->text,p->selection);
                auto lo=std::min(p->selection.anchor,p->selection.caret),hi=std::max(p->selection.anchor,p->selection.caret);
                p->selection=p->handle==0?ocr::Selection{hi,lo}:ocr::Selection{lo,hi};
                const auto b=p->handle==0?boxes.front():boxes.back();
                p->handle_offset={(p->handle==0?b.left:b.right)-point.x,(b.top+b.bottom)/2-point.y};p->selecting=true;p->blank_click=false;p->clicks=0;
                SetCapture(window);p->manager->UpdateTools(*p);return 0;
            }
            const bool on_text=ocr::OnText(p->text,point);
            if(message==WM_LBUTTONDBLCLK&&!p->locked&&!space&&p->blank_click&&!p->dragged&&p->Blank(point)){DestroyWindow(window);return 0;}
            p->blank_click=!space&&p->Blank(point);p->dragged=false;
            if(space||!on_text) {
                if(p->locked)return 0;
                if(!space)p->selection={};
                p->moving=true;GetCursorPos(&p->drag);RECT r{};GetWindowRect(window,&r);p->origin={r.left,r.top};p->clicks=0;
            }else {
                const DWORD now=GetTickCount();const POINT local{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
                p->clicks=now-p->click_time<=GetDoubleClickTime()&&std::abs(local.x-p->last_click.x)<=GetSystemMetrics(SM_CXDOUBLECLK)&&std::abs(local.y-p->last_click.y)<=GetSystemMetrics(SM_CYDOUBLECLK)?p->clicks%3+1:1;
                p->click_time=now;p->last_click=local;
                if(p->clicks>=2)p->selection=ocr::Word(p->text,point,p->clicks==3);
                else {const auto hit=ocr::Hit(p->text,point);p->selection={hit,hit};p->selecting=true;}
            }SetCapture(window);p->manager->UpdateTools(*p);InvalidateRect(window,nullptr,FALSE);return 0;}
        case WM_MOUSEMOVE:
            if(p->moving){POINT pt{};GetCursorPos(&pt);if(std::abs(pt.x-p->drag.x)>=GetSystemMetrics(SM_CXDRAG)||std::abs(pt.y-p->drag.y)>=GetSystemMetrics(SM_CYDRAG))p->dragged=true;SetWindowPos(window,nullptr,p->origin.x+pt.x-p->drag.x,p->origin.y+pt.y-p->drag.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);}
            if(p->selecting){auto point=p->ImagePoint(lp);if(p->handle>=0){point.x+=p->handle_offset.x;point.y+=p->handle_offset.y;}p->selection.caret=ocr::Hit(p->text,point,true);InvalidateRect(window,nullptr,FALSE);}return 0;
        case WM_LBUTTONUP:if(p->selecting)SendMessageW(window,WM_MOUSEMOVE,wp,lp);p->moving=p->selecting=false;p->handle=-1;if(GetCapture()==window)ReleaseCapture();p->manager->UpdateTools(*p);p->manager->SessionChanged();InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_CAPTURECHANGED:p->moving=p->selecting=false;p->handle=-1;p->manager->UpdateTools(*p);return 0;
        case WM_CONTEXTMENU:p->manager->Menu(*p,{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});return 0;
        case WM_KEYDOWN:
            if(!(GetKeyState(VK_CONTROL)&0x8000)&&!(GetKeyState(VK_MENU)&0x8000)&&!(lp&(1LL<<30))){
                // Escape first dismisses a text selection; only a second Escape closes the pin.
                if(wp==VK_ESCAPE&&!p->selection.Empty()){p->selection={};p->manager->UpdateTools(*p);InvalidateRect(window,nullptr,FALSE);return 0;}
                if(wp==VK_ESCAPE){SendMessageW(window,WM_CLOSE,0,0);return 0;}
                if(wp==VK_SPACE){p->manager->Annotate(*p);return 0;}
                if(wp=='L'){p->manager->ToggleLock(*p);return 0;}
                if(wp=='T'&&ocr::Available()&&!p->editing){p->manager->TranslateKey(*p);return 0;}
            }
            if(GetKeyState(VK_CONTROL)&0x8000){if(wp=='A'){p->selection=ocr::All(p->text);p->manager->UpdateTools(*p);InvalidateRect(window,nullptr,FALSE);}if(wp=='C')CopyText(window,ocr::Selected(p->text,p->selection));if(wp=='Z'&&!p->undo.empty()){p->marks=std::move(p->undo.back());p->undo.pop_back();++p->revision;p->manager->SessionChanged();InvalidateRect(window,nullptr,FALSE);}}return 0;
        case WM_QUERYENDSESSION:p->manager->FlushSession();return TRUE;
        case WM_ENDSESSION:if(wp)p->manager->PreserveSession();return 0;
        case WM_CLOSE:DestroyWindow(window);return 0;
        case WM_DESTROY:KillTimer(window,103);KillTimer(window,101);KillTimer(window,102);p->zoom_animating=false;p->moving=p->selecting=false;p->selection={};return 0;
        case WM_NCDESTROY:if(p->tools)DestroyWindow(p->tools);if(p->tr.job&&p->manager->translator_)p->manager->translator_->Cancel(p->tr.job);p->tr.job=0;p->tr.image.reset();p->manager->service_.Cancel(p->id);p->window=nullptr;if(!p->manager->preserving_session_&&!p->manager->restoring_session_){p->manager->SessionChanged();p->manager->FlushSession();}p->image.reset();p->ocr_image.reset();p->text={};p->target.Reset();p->bitmap.Reset();SetWindowLongPtrW(window,GWLP_USERDATA,0);PostMessageW(p->manager->host_,kOcrReady,0,0);break;
        }
    }catch(const std::exception& e){ui::ShowThemedMessage(window,p->manager->dark_theme&&p->manager->dark_theme(),L"LumaShot",ocr::ErrorMessage(e));}
    return DefWindowProcW(window,message,wp,lp);
}
PinManager::Pin* PinManager::FindPin(uint64_t id){const auto it=pins_.find(id);return it!=pins_.end()&&it->second->window?it->second.get():nullptr;}
void PinManager::TranslateLast(){if(auto* p=FindPin(last_created_))TranslatePin(*p);}
void PinManager::TranslatePin(Pin& p){
    using State=TranslationView::State;
    if(p.tr.state==State::Done){if(!p.tr.show)ShowTranslation(p,true);UpdatePanel(p);return;}
    if(p.tr.state==State::Running||p.tr.state==State::WaitingOcr){UpdatePanel(p);return;}
    StartOrWait(p);
}
void PinManager::TranslateKey(Pin& p){
    if(p.tr.state==TranslationView::State::Done){ShowTranslation(p,!p.tr.show);return;}
    TranslatePin(p);
}
void PinManager::StartOrWait(Pin& p){
    using State=TranslationView::State;
    const auto config=translate::LoadConfig(translate::DefaultConfigPath());
    std::wstring reason;
    if(!translate::Resolve(config,&reason)){
        p.tr.state=State::NeedsSetup;p.tr.error=reason;p.tr.engine.clear();UpdatePanel(p);
        // First use: open the guided settings once per session; later the panel offers it.
        if(!settings_prompted_&&open_translation_settings){settings_prompted_=true;open_translation_settings();}
        return;
    }
    if(!ocr::Available()){p.tr.state=State::Failed;p.tr.error=L"文字识别组件不可用，无法翻译";UpdatePanel(p);return;}
    if(!p.ocr_enabled||p.recognizing){if(!p.ocr_enabled)Recognize(p);p.tr.state=State::WaitingOcr;UpdatePanel(p);return;}
    StartTranslation(p);
}
void PinManager::StartTranslation(Pin& p){
    using State=TranslationView::State;
    const auto config=translate::LoadConfig(translate::DefaultConfigPath());
    std::wstring reason;auto credentials=translate::Resolve(config,&reason);
    if(!credentials){p.tr.state=State::NeedsSetup;p.tr.error=reason;UpdatePanel(p);return;}
    p.tr.engine=std::wstring(credentials->preset->label);
    p.tr.blocks=translate::BuildBlocks(p.text);p.tr.results.clear();
    if(p.tr.blocks.empty()){p.tr.state=State::Failed;p.tr.error=L"图片中没有识别到可翻译的文字";UpdatePanel(p);return;}
    std::wstring all;for(const auto& block:p.tr.blocks){all+=block.text;all+=L'\n';}
    p.tr.source=translate::Detect(all);
    p.tr.target=translate::ResolveTarget(p.tr.requested!=translate::Language::Auto?p.tr.requested:config.target,p.tr.source);
    translate::Job job;job.source=p.tr.source;job.target=p.tr.target;
    for(const auto& block:p.tr.blocks)job.texts.push_back(block.text);
    if(!translator_)translator_=std::make_unique<translate::Service>(host_,kTranslateReady);
    if(p.tr.job)translator_->Cancel(p.tr.job);
    p.tr.job=translator_->Submit(std::move(*credentials),std::move(job));
    p.tr.state=State::Running;p.tr.error.clear();UpdatePanel(p);
}
void PinManager::TranslationReady(uint64_t job){
    if(!translator_||!job)return;
    auto outcome=translator_->Take(job);
    if(!outcome)return;
    Pin* found=nullptr;for(auto& [id,pin]:pins_)if(pin->window&&pin->tr.job==job){found=pin.get();break;}
    if(!found)return;
    auto& p=*found;p.tr.job=0;
    using State=TranslationView::State;
    if(!outcome->ok||outcome->texts.size()!=p.tr.blocks.size()){p.tr.state=State::Failed;p.tr.error=outcome->error.empty()?L"翻译服务没有返回结果":outcome->error;UpdatePanel(p);return;}
    p.tr.results=std::move(outcome->texts);
    try{p.tr.image=std::make_shared<Frame>(pin_translation::Render(*p.image,p.tr.blocks,p.tr.results,p.tr.target));}
    catch(const std::exception& e){p.tr.image.reset();p.tr.state=State::Failed;p.tr.error=L"译文渲染失败："+ocr::ErrorMessage(e);UpdatePanel(p);return;}
    p.tr.state=State::Done;ShowTranslation(p,true);
}
void PinManager::TranslationConfigChanged(){
    using State=TranslationView::State;
    for(auto& [id,pin]:pins_)if(pin->window&&(pin->tr.state==State::NeedsSetup||(pin->tr.state==State::Failed&&pin->tr.panel&&pin->tr.panel->Visible())))StartOrWait(*pin);
}
void PinManager::ResetTranslation(Pin& p){
    if(p.tr.job&&translator_)translator_->Cancel(p.tr.job);
    const bool had_panel=p.tr.panel&&p.tr.panel->Visible();
    auto panel=std::move(p.tr.panel);const auto requested=p.tr.requested;
    p.tr={};p.tr.requested=requested;p.tr.panel=std::move(panel);
    if(had_panel)p.tr.panel->Hide();
}
void PinManager::ShowTranslation(Pin& p,bool show){
    p.tr.show=show&&p.tr.image;p.bitmap.Reset();p.selection={};UpdateTools(p);
    if(p.window)Paint(p);
    UpdatePanel(p);
}
void PinManager::EnsurePanel(Pin& p){
    if(p.tr.panel&&p.tr.panel->Window())return;
    const auto id=p.id;
    TranslationPanel::Callbacks callbacks;
    callbacks.show_translation=[this,id](bool show){if(auto* pin=FindPin(id))ShowTranslation(*pin,show);};
    callbacks.retarget=[this,id](translate::Language language){
        auto* pin=FindPin(id);if(!pin)return;pin->tr.requested=language;
        using State=TranslationView::State;
        if(pin->tr.state==State::Done||pin->tr.state==State::Failed){pin->tr.show=false;pin->tr.image.reset();pin->bitmap.Reset();Paint(*pin);StartOrWait(*pin);}
        else UpdatePanel(*pin);
    };
    callbacks.retry=[this,id]{if(auto* pin=FindPin(id))StartOrWait(*pin);};
    callbacks.settings=[this]{if(open_translation_settings)open_translation_settings();};
    // Closing only hides the panel; the translated rendering stays on the pin.
    callbacks.closed=[this,id]{if(auto* pin=FindPin(id);pin&&pin->tr.panel)pin->tr.panel->Hide();};
    p.tr.panel=std::make_unique<TranslationPanel>(p.window,std::move(callbacks));
}
void PinManager::UpdatePanel(Pin& p){
    if(!p.window)return;
    EnsurePanel(p);
    TranslationView view;view.state=p.tr.state;view.engine=p.tr.engine;view.error=p.tr.error;view.source=p.tr.source;view.target=p.tr.target;view.requested=p.tr.requested;
    view.show_translation=p.tr.show;view.dark=dark_theme&&dark_theme();
    if(p.tr.state==TranslationView::State::Done){for(const auto& block:p.tr.blocks)view.sources.push_back(block.text);view.results=p.tr.results;}
    else if(p.tr.state==TranslationView::State::Running){for(const auto& block:p.tr.blocks)view.sources.push_back(block.text);}
    p.tr.panel->Update(view);
    RECT r{};GetWindowRect(p.window,&r);
    const LONG left=r.left+p.paper.shadow+LONG(p.offset.x),top=r.top+p.paper.shadow+LONG(p.offset.y);
    p.tr.panel->Place({left,top,left+p.paper.width,top+p.paper.height});
    p.tr.panel->Show();
}
void PinManager::PlacePanel(Pin& p){
    if(!p.window||!p.tr.panel||!p.tr.panel->Visible())return;
    RECT r{};GetWindowRect(p.window,&r);
    // During ApplyPaper the window has not moved yet; use the pending destination.
    const POINT origin=p.relocating?p.destination:POINT{r.left,r.top};
    const LONG left=origin.x+p.paper.shadow+LONG(p.offset.x),top=origin.y+p.paper.shadow+LONG(p.offset.y);
    p.tr.panel->Place({left,top,left+p.paper.width,top+p.paper.height});
}
}


