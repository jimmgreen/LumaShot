#include "ui/render.h"
#include "model/arrow.h"
#include "model/note_color.h"
#include "model/selection.h"
#include "ui/selection_render.h"
#include "ui/acrylic.h"
#include "capture/cursor.h"
#include "app/diagnostics.h"
#include <wincodec.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <system_error>

namespace lumashot {
static D2D1_RECT_F Rect(Box b) { return {b.left, b.top, b.right, b.bottom}; }
static Box BoxOf(RECT r) { return {float(r.left), float(r.top), float(r.right), float(r.bottom)}; }
static D2D1_POINT_2F Pt(Point p) { return {p.x, p.y}; }
static void Check(HRESULT hr) {
    if (FAILED(hr)) throw std::system_error(static_cast<int>(hr), std::system_category(), "Rendering");
}

Renderer::Renderer() {
    Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf()));
    Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(text_factory_.GetAddressOf())));
}
void Renderer::Brush(uint32_t color, float opacity) {
    const auto c = D2D1::ColorF(color & 0x00ffffff, float(color >> 24) / 255 * opacity);
    if (!brush_) Check(target_->CreateSolidColorBrush(c, &brush_));
    else brush_->SetColor(c);
}
IDWriteTextFormat* Renderer::FontFormat(float size,bool wrap,bool bold,TextAlign align,const std::wstring& family) {
    auto found=std::find_if(text_formats_.begin(),text_formats_.end(),[&](const auto& entry){return entry.size==size&&entry.wrap==wrap&&entry.bold==bold&&entry.align==align&&entry.family==family;});
    if(found==text_formats_.end()) {
        ComPtr<IDWriteTextFormat> format;
        Check(text_factory_->CreateTextFormat(family.c_str(), nullptr, bold?DWRITE_FONT_WEIGHT_BOLD:DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", &format));
        if(text_formats_.size()>=32)text_formats_.erase(text_formats_.begin());
        Check(format->SetWordWrapping(wrap?DWRITE_WORD_WRAPPING_WRAP:DWRITE_WORD_WRAPPING_NO_WRAP));
        Check(format->SetTextAlignment(align==TextAlign::Center?DWRITE_TEXT_ALIGNMENT_CENTER:(align==TextAlign::Right?DWRITE_TEXT_ALIGNMENT_TRAILING:DWRITE_TEXT_ALIGNMENT_LEADING)));
        text_formats_.push_back({size,wrap,bold,align,family,std::move(format)});found=std::prev(text_formats_.end());
    }
    return found->format.Get();
}
Point Renderer::MeasureText(const std::wstring& text,float size) {
    ComPtr<IDWriteTextLayout> layout;
    Check(text_factory_->CreateTextLayout(text.data(),static_cast<UINT32>(text.size()),FontFormat(size,false),4096,512,&layout));
    DWRITE_TEXT_METRICS metrics{};Check(layout->GetMetrics(&metrics));
    return {std::ceil(metrics.widthIncludingTrailingWhitespace),std::ceil(metrics.height)};
}
void Renderer::Text(const std::wstring& text, Box rect, float size, uint32_t color,bool wrap) {
    text_renderer_.Draw(target_.Get(),text_factory_.Get(),text,FontFormat(size,wrap),Rect(rect),
        D2D1::ColorF(color&0x00ffffff,float(color>>24)/255));
}
ComPtr<ID2D1Bitmap> Renderer::Bitmap(const Frame& frame,D2D1_ALPHA_MODE alpha) {
    ComPtr<ID2D1Bitmap> result;
    Check(target_->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(frame.Width()), static_cast<UINT32>(frame.Height())),
        frame.pixels.data(), static_cast<UINT32>(frame.Width()) * 4,
        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, alpha)), &result));
    return result;
}

ComPtr<ID2D1Bitmap> Renderer::BitmapRegion(const Frame& frame,RECT region) {
    RECT clipped{};
    if(!IntersectRect(&clipped,&frame.bounds,&region))throw std::invalid_argument("Bitmap region does not intersect capture");
    const size_t offset=static_cast<size_t>(clipped.top-frame.bounds.top)*frame.Width()+clipped.left-frame.bounds.left;
    ComPtr<ID2D1Bitmap> result;
    // Direct2D copies each row synchronously, honoring the original desktop stride.
    Check(target_->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(clipped.right-clipped.left),static_cast<UINT32>(clipped.bottom-clipped.top)),
        frame.pixels.data()+offset,static_cast<UINT32>(frame.Width())*4,
        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE)),&result));
    return result;
}

void Renderer::MarkShape(const Frame& frame, const Mark& mark, size_t index) {
    D2D1_MATRIX_3X2_F old;target_->GetTransform(&old);
    struct Restore {ID2D1RenderTarget* t;D2D1_MATRIX_3X2_F matrix;~Restore(){t->SetTransform(matrix);}} restore{target_.Get(),old};
    const auto center=MarkCenter(mark);target_->SetTransform(D2D1::Matrix3x2F::Rotation(mark.rotation,{center.x,center.y})*old);
    Brush(mark.color);
    const Box b = LocalBounds(mark);
    const auto rect = Rect(b);
    ComPtr<ID2D1StrokeStyle> stroke;
    if((mark.tool==Tool::Rectangle||mark.tool==Tool::Arrow)&&mark.line_style!=LineStyle::Solid){
        auto props=D2D1::StrokeStyleProperties();
        props.dashStyle=mark.line_style==LineStyle::Dash?D2D1_DASH_STYLE_DASH:(mark.line_style==LineStyle::Dot?D2D1_DASH_STYLE_DOT:D2D1_DASH_STYLE_DASH_DOT);
        props.dashCap=D2D1_CAP_STYLE_ROUND;
        Check(factory_->CreateStrokeStyle(props,nullptr,0,&stroke));
    }
    switch (mark.tool) {
    case Tool::Number:NumberDetail(frame,mark);Number(frame,mark);break;
    case Tool::Rectangle: {
        auto radii=mark.corner_radii.value_or(std::array<float,4>{mark.corner_radius,mark.corner_radius,mark.corner_radius,mark.corner_radius});for(auto& r:radii)r=std::clamp(r,0.f,std::min(b.right-b.left,b.bottom-b.top)/2);
        ComPtr<ID2D1PathGeometry> shape;Check(factory_->CreatePathGeometry(&shape));ComPtr<ID2D1GeometrySink> sink;Check(shape->Open(&sink));
        sink->BeginFigure({b.left+radii[0],b.top},D2D1_FIGURE_BEGIN_FILLED);sink->AddLine({b.right-radii[1],b.top});sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment({b.right,b.top},{b.right,b.top+radii[1]}));
        sink->AddLine({b.right,b.bottom-radii[2]});sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment({b.right,b.bottom},{b.right-radii[2],b.bottom}));
        sink->AddLine({b.left+radii[3],b.bottom});sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment({b.left,b.bottom},{b.left,b.bottom-radii[3]}));
        sink->AddLine({b.left,b.top+radii[0]});sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment({b.left,b.top},{b.left+radii[0],b.top}));sink->EndFigure(D2D1_FIGURE_END_CLOSED);Check(sink->Close());
        if(mark.fill_color){Brush(*mark.fill_color,mark.fill_opacity);target_->FillGeometry(shape.Get(),brush_.Get());}
        Brush(mark.color);target_->DrawGeometry(shape.Get(),brush_.Get(),mark.width,stroke.Get());break;
    }
    case Tool::Ellipse: {
        const auto shape=D2D1::Ellipse(D2D1::Point2F((b.left+b.right)/2,(b.top+b.bottom)/2),(b.right-b.left)/2,(b.bottom-b.top)/2);
        if(mark.fill_color){Brush(*mark.fill_color,mark.fill_opacity);target_->FillEllipse(shape,brush_.Get());}
        Brush(mark.color);target_->DrawEllipse(shape,brush_.Get(),mark.width);break;
    }
    case Tool::Arrow: {
        auto props=D2D1::StrokeStyleProperties();props.startCap=props.endCap=D2D1_CAP_STYLE_ROUND;props.lineJoin=D2D1_LINE_JOIN_ROUND;
        ComPtr<ID2D1StrokeStyle> outline;Check(factory_->CreateStrokeStyle(props,nullptr,0,&outline));
        for(const auto& path:BuildArrow(mark)){
            if(path.points.empty())continue;
            ComPtr<ID2D1PathGeometry> geometry;Check(factory_->CreatePathGeometry(&geometry));
            ComPtr<ID2D1GeometrySink> sink;Check(geometry->Open(&sink));
            sink->BeginFigure(Pt(path.points.front()),path.filled?D2D1_FIGURE_BEGIN_FILLED:D2D1_FIGURE_BEGIN_HOLLOW);
            for(size_t i=1;i<path.points.size();++i)sink->AddLine(Pt(path.points[i]));
            sink->EndFigure(path.closed?D2D1_FIGURE_END_CLOSED:D2D1_FIGURE_END_OPEN);Check(sink->Close());
            if(path.filled)target_->FillGeometry(geometry.Get(),brush_.Get());
            else target_->DrawGeometry(geometry.Get(),brush_.Get(),mark.width,path.shaft&&stroke?stroke.Get():outline.Get());
        }
        break;
    }
    case Tool::Pen: {
        Brush(mark.color,std::clamp(mark.pen_opacity,0.f,1.f));
        if(mark.points.size()==1){
            const auto p=mark.points[0];const float half=mark.width/2;
            if(mark.pen_mode==PenMode::Highlighter)target_->FillRectangle(D2D1::RectF(p.x-half,p.y-half,p.x+half,p.y+half),brush_.Get());
            else target_->FillEllipse(D2D1::Ellipse(Pt(p),half,half),brush_.Get());
        }
        if(mark.points.size()>1){
            ComPtr<ID2D1PathGeometry> path;Check(factory_->CreatePathGeometry(&path));
            ComPtr<ID2D1GeometrySink> sink;Check(path->Open(&sink));
            sink->BeginFigure(Pt(mark.points.front()),D2D1_FIGURE_BEGIN_HOLLOW);
            const float amount=(mark.pen_straight||mark.pen_polyline||mark.pen_smoothing==PenSmoothing::None)?0.f:(mark.pen_smoothing==PenSmoothing::Standard?.25f:.5f);
            for(size_t i=1;i+1<mark.points.size();++i){
                const auto p=mark.points[i],before=mark.points[i-1],after=mark.points[i+1];
                if(amount==0){sink->AddLine(Pt(p));continue;}
                sink->AddLine({p.x+(before.x-p.x)*amount,p.y+(before.y-p.y)*amount});
                sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(Pt(p),{p.x+(after.x-p.x)*amount,p.y+(after.y-p.y)*amount}));
            }
            sink->AddLine(Pt(mark.points.back()));sink->EndFigure(D2D1_FIGURE_END_OPEN);Check(sink->Close());
            auto props=D2D1::StrokeStyleProperties();props.startCap=props.endCap=D2D1_CAP_STYLE_ROUND;props.lineJoin=D2D1_LINE_JOIN_ROUND;
            if(mark.pen_mode==PenMode::Highlighter)props.startCap=props.endCap=D2D1_CAP_STYLE_FLAT;
            ComPtr<ID2D1StrokeStyle> pen_style;Check(factory_->CreateStrokeStyle(props,nullptr,0,&pen_style));
            target_->DrawGeometry(path.Get(),brush_.Get(),mark.width,pen_style.Get());
        }
        break;
    }
    case Tool::Text:{
        const auto appearance=ResolveTextAppearance(frame,mark);
        if(appearance.background){
            if(IsTextTag(mark.text_background))TagBackground(b,*appearance.background,appearance.border,TextTagMetrics(mark.font_size));
            else {Brush(*appearance.background);target_->FillRectangle(rect,brush_.Get());}
        }
        text_renderer_.Draw(target_.Get(),text_factory_.Get(),mark.text,FontFormat(mark.font_size,true,mark.text_bold,mark.text_align,mark.font_family),Rect(TextContentBounds(mark)),
            D2D1::ColorF(appearance.ink&0xffffff,float(appearance.ink>>24)/255));break;}
    case Tool::Mosaic: {
        auto& cache=mosaic_cache_[index];
        if(cache.pixels!=frame.pixels.data() || !(cache.mark==mark)) {
            cache.bitmap.Reset();
            if(mark.mosaic_mode==MosaicMode::Blur){cache.blur=BuildMosaicBlur(frame,mark);cache.tiles.clear();}
            else {cache.tiles=BuildMosaicTiles(frame,mark);cache.blur={};}
            cache.mark=mark;cache.pixels=frame.pixels.data();
        }
        if(mark.mosaic_mode==MosaicMode::Blur){if(!cache.blur.pixels.empty()){if(!cache.bitmap)cache.bitmap=Bitmap(cache.blur,D2D1_ALPHA_MODE_PREMULTIPLIED);target_->DrawBitmap(cache.bitmap.Get(),Rect(BoxOf(cache.blur.bounds)),1,D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);}break;}
        target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        for(const auto& tile:cache.tiles) {
            Brush(tile.color);target_->FillRectangle(Rect(BoxOf(tile.bounds)),brush_.Get());
        }
        target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);break;
    }
    default: break;
    }
}
void Renderer::Marks(const Frame& frame, const Document& document, const std::optional<Mark>& draft,int editing_text) {
    mosaic_cache_.resize(document.marks.size()+1);
    for(size_t i=0;i<document.marks.size();++i) {
        const auto& mark=document.marks[i];
        // The in-place editor paints the live copy (ViewState::text_edit) on
        // top; never draw the committed version underneath it.
        if(editing_text>=0&&i==static_cast<size_t>(editing_text))continue;
        MarkShape(frame,mark,i);
    }
    if (draft) MarkShape(frame,*draft,document.marks.size());
}

void Renderer::Panel(Box rect, ID2D1Bitmap* acrylic, const Frame& frame, bool dark, float radius,bool acrylic_surface) {
    const auto rr = D2D1::RoundedRect(Rect(rect), radius, radius);
    Brush(0x22000000);
    auto shadow = rect; shadow.top += 3; shadow.bottom += 4;
    target_->FillRoundedRectangle(D2D1::RoundedRect(Rect(shadow),radius,radius),brush_.Get());
    ComPtr<ID2D1RoundedRectangleGeometry> geometry;
    Check(factory_->CreateRoundedRectangleGeometry(rr, &geometry));
    target_->PushLayer(D2D1::LayerParameters(Rect(rect),geometry.Get()),nullptr);
    if(acrylic_surface) {
        const auto bitmap_size=acrylic->GetSize();
        const float sx=bitmap_size.width/float(frame.Width()),sy=bitmap_size.height/float(frame.Height());
        target_->DrawBitmap(acrylic,Rect(rect),1,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
            D2D1::RectF((rect.left-frame.bounds.left)*sx,(rect.top-frame.bounds.top)*sy,
                (rect.right-frame.bounds.left)*sx,(rect.bottom-frame.bounds.top)*sy));
        Brush(dark ? 0x9423354b : 0x80eaf3ff);  // tint over the blurred capture: light 50%, dark 58% (was 77% / 66%)
        target_->FillRectangle(Rect(rect),brush_.Get());
    } else {
        Brush(0xffffffff);
        target_->FillRoundedRectangle(rr,brush_.Get());
    }
    target_->PopLayer();
    Brush(acrylic_surface?(dark ? 0x557b8da5 : 0xd9ffffff):0xffcbd5e0);
    target_->DrawRoundedRectangle(rr,brush_.Get(),1);
}

void Renderer::Icon(int index, Box rect, uint32_t color) {
    const float unit=(rect.right-rect.left)/32;
    const float cx=(rect.left+rect.right)/2, cy=(rect.top+rect.bottom)/2;
    Brush(color);
    const auto line=[&](float x1,float y1,float x2,float y2) {
        target_->DrawLine({cx+x1*unit,cy+y1*unit},{cx+x2*unit,cy+y2*unit},brush_.Get(),1.65f*unit);
    };
    switch(index) {
    case 19:
        // More: three dots.
        for(const float x:{-7.f,0.f,7.f})target_->FillEllipse(D2D1::Ellipse({cx+x*unit,cy},1.9f*unit,1.9f*unit),brush_.Get());break;
    case 18:
        // Translate: a Han-like glyph on the left, a Latin "A" on the right.
        line(-10,-7,-1,-7);line(-5.5f,-10,-5.5f,-7);line(-9,-4,-2,3);line(-2,-4,-9,3);
        line(1,9,5,-2);line(5,-2,9,9);line(2.6f,5,7.4f,5);break;
    case 17:
        // Long capture: a page continuing below the frame, with a downward arrow.
        line(-7,-9,7,-9);line(-7,-9,-7,1);line(7,-9,7,1);line(-7,4,-7,6);line(7,4,7,6);
        line(0,-5,0,9);line(-4,5,0,9);line(4,5,0,9);break;
    case 16:
        target_->DrawRoundedRectangle(D2D1::RoundedRect({cx-9*unit,cy-6*unit,cx+3*unit,cy+6*unit},2*unit,2*unit),brush_.Get(),1.65f*unit);
        line(3,-3,9,-6);line(9,-6,9,6);line(9,6,3,3);
        Brush((color&0xff000000)|0x00ef4444);
        target_->FillEllipse(D2D1::Ellipse({cx-3*unit,cy},2.5f*unit,2.5f*unit),brush_.Get());break;
    case 14:{Mark mark;mark.tool=Tool::Number;mark.number_size=20*unit;mark.a={cx-10*unit,cy-10*unit};mark.b={cx+10*unit,cy+10*unit};mark.number_shape=NumberShape::Outline;mark.color=color;Number(Frame{},mark);break;}
    case 51:
        text_renderer_.Draw(target_.Get(),text_factory_.Get(),L"B",FontFormat(20*unit,false,true,TextAlign::Center),Rect(rect),D2D1::ColorF(color&0xffffff,float(color>>24)/255));break;
    case 53:case 54:case 55:
        for(int row=0;row<3;++row){const float length=row%2?12.f:18.f;const float left=index==53?-9.f:(index==54?-length/2:9.f-length);line(left,-5.f+row*5,left+length,-5.f+row*5);}break;
    case 0: line(-5,-8,-5,7);line(-5,-8,7,2);line(-5,7,0,3);line(0,3,4,9);line(7,2,0,3);break;
    case 1: target_->DrawRectangle({cx-7*unit,cy-6*unit,cx+7*unit,cy+6*unit},brush_.Get(),1.65f*unit);break;
    case 2: target_->DrawEllipse(D2D1::Ellipse({cx,cy},7*unit,7*unit),brush_.Get(),1.65f*unit);break;
    case 3: line(-7,7,7,-7);line(-1,-7,7,-7);line(7,-7,7,1);break;
    case 4: {
        ComPtr<ID2D1PathGeometry> path;Check(factory_->CreatePathGeometry(&path));ComPtr<ID2D1GeometrySink> sink;Check(path->Open(&sink));
        const auto p=[&](float x,float y){return D2D1_POINT_2F{cx+x*unit,cy+y*unit};};
        sink->BeginFigure(p(0,0),D2D1_FIGURE_BEGIN_HOLLOW);sink->AddLine(p(7,-8));sink->AddBezier(D2D1::BezierSegment(p(9,-10),p(11,-8),p(9,-6)));sink->AddLine(p(3,2));sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->BeginFigure(p(-1,2),D2D1_FIGURE_BEGIN_HOLLOW);sink->AddBezier(D2D1::BezierSegment(p(-7,2),p(-3,8),p(-9,9)));sink->AddBezier(D2D1::BezierSegment(p(-2,11),p(2,7),p(2,5)));sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        Check(sink->Close());target_->DrawGeometry(path.Get(),brush_.Get(),1.65f*unit);line(-1,0,4,4);break;
    }
    case 5: line(-7,-7,7,-7);line(0,-7,0,8);break;
    case 6:
        for(int y=0;y<4;++y)for(int x=0;x<4;++x)if((x+y)%2==0)
            target_->FillRectangle({cx+(x*4-8)*unit,cy+(y*4-8)*unit,
                cx+(x*4-4)*unit,cy+(y*4-4)*unit},brush_.Get());break;
    case 7: case 8: {
        const float flip=index==7 ? 1.0f : -1.0f;
        ComPtr<ID2D1PathGeometry> curve;Check(factory_->CreatePathGeometry(&curve));
        ComPtr<ID2D1GeometrySink> sink;Check(curve->Open(&sink));
        sink->BeginFigure({cx-7*flip*unit,cy-3*unit},D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine({cx+1*flip*unit,cy-3*unit});
        sink->AddBezier(D2D1::BezierSegment({cx+10*flip*unit,cy-3*unit},{cx+10*flip*unit,cy+8*unit},{cx+1*flip*unit,cy+8*unit}));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);Check(sink->Close());target_->DrawGeometry(curve.Get(),brush_.Get(),1.65f*unit);
        line(-7*flip,-3,-2*flip,-8);line(-7*flip,-3,-2*flip,2);break;
    }
    case 9:
        target_->DrawRectangle({cx-7*unit,cy-8*unit,cx+7*unit,cy+8*unit},brush_.Get(),1.6f*unit);
        line(-3,-8,-3,-2);line(-3,-2,4,-2);line(4,-2,4,-8);line(-3,8,-3,3);line(-3,3,4,3);line(4,3,4,8);break;
    case 10:
        target_->DrawRoundedRectangle(D2D1::RoundedRect({cx-7*unit,cy-4*unit,cx+3*unit,cy+8*unit},unit,unit),brush_.Get(),1.6f*unit);
        line(-3,-8,7,-8);line(7,-8,7,4);break;
    case 11:line(-6,-6,6,6);line(6,-6,-6,6);break;
    case 12:line(-7,0,-1,6);line(-1,6,8,-6);break;
    case 13:line(-5,-7,5,-7);line(-3,-7,-3,0);line(3,-7,3,0);line(-6,2,6,2);line(-3,0,-6,2);line(3,0,6,2);line(0,2,0,9);break;
    default:break;
    }
}

void Renderer::TextEditOverlay(const Frame& frame,const ViewState& state){
    const auto& edit=state.text_edit;const Mark& mark=*edit.mark;
    const float scale=std::max(.5f,state.toolbar.scale);
    const auto geometry=MeasureTextEdit(mark,edit.selection_start,edit.selection_end,edit.caret,edit.composition_start,edit.composition_length);
    D2D1_MATRIX_3X2_F old;target_->GetTransform(&old);
    struct Restore {ID2D1RenderTarget* t;D2D1_MATRIX_3X2_F matrix;~Restore(){t->SetTransform(matrix);}} restore{target_.Get(),old};
    const auto center=MarkCenter(mark);target_->SetTransform(D2D1::Matrix3x2F::Rotation(mark.rotation,{center.x,center.y})*old);
    const uint32_t ink=mark.tool==Tool::Number?ResolveNoteAppearance(frame,mark).ink:ResolveTextAppearance(frame,mark).ink;
    // Selection: a translucent system-blue wash keeps the final glyphs legible.
    Brush(0xff2f86ff,.34f);
    for(const auto& part:geometry.selection)target_->FillRectangle({part.left,part.top,std::max(part.right,part.left+2*scale),part.bottom},brush_.Get());
    // IME preedit: underline in the text colour, like native composition.
    Brush(ink|0xff000000);
    const float underline=std::max(1.f,std::round(1.5f*scale));
    for(const auto& part:geometry.composition)target_->FillRectangle({part.left,part.bottom-underline,part.right,part.bottom},brush_.Get());
    // Dashed outline marks the active edit box without covering the screenshot.
    const float gap=4*scale;
    const D2D1_RECT_F outline{geometry.frame.left-gap,geometry.frame.top-gap,geometry.frame.right+gap,geometry.frame.bottom+gap};
    Brush(0xffffffff,.75f);target_->DrawRectangle(outline,brush_.Get(),2.5f*scale);
    auto props=D2D1::StrokeStyleProperties();props.dashStyle=D2D1_DASH_STYLE_CUSTOM;
    const float dashes[]{4,3};ComPtr<ID2D1StrokeStyle> dashed;Check(factory_->CreateStrokeStyle(props,dashes,2,&dashed));
    Brush(0xff248dff);target_->DrawRectangle(outline,brush_.Get(),std::max(1.f,1.25f*scale),dashed.Get());
    if(edit.caret_visible&&edit.selection_start==edit.selection_end){
        const float width=std::clamp(std::round((mark.tool==Tool::Number?mark.number_text_size:mark.font_size)/8)/2,1.5f,3.f);
        const float x=std::round(geometry.caret.left-width/2);
        Brush(ink|0xff000000);target_->FillRectangle({x,geometry.caret.top,x+width,geometry.caret.bottom},brush_.Get());
    }
}

void Renderer::Chrome(const Frame& frame, const Frame&, const Document& document,
    const std::optional<Mark>& draft, const ViewState& state, RECT monitor,int editing_text) {
    target_->DrawBitmap(screen_bitmap_.Get(),Rect(BoxOf(monitor)),1,D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    const Box desktop=BoxOf(frame.bounds), b=state.selection;
    Brush(0x790a1220);
    if (!state.selected && !state.dragging && b.right<=b.left) target_->FillRectangle(Rect(desktop),brush_.Get());
    else {
        if(!state.clear_outside)for(const Box mask : {Box{desktop.left,desktop.top,desktop.right,b.top},
            Box{desktop.left,b.bottom,desktop.right,desktop.bottom},
            Box{desktop.left,b.top,b.left,b.bottom},Box{b.right,b.top,desktop.right,b.bottom}}) {
            if(mask.right>mask.left && mask.bottom>mask.top)target_->FillRectangle(Rect(mask),brush_.Get());
        }
        target_->PushAxisAlignedClip(Rect(b),D2D1_ANTIALIAS_MODE_ALIASED);
        Marks(frame,document,draft,editing_text);
        // Text being typed is painted exactly like the committed annotation.
        if(state.text_edit.mark)MarkShape(frame,*state.text_edit.mark,document.marks.size());
        SnapGuide(state,draft);
        target_->PopAxisAlignedClip();
        if(state.text_edit.mark)TextEditOverlay(frame,state);
        // Over a pin (clear_outside) the selection is the pin itself and is never resized: keep the
        // border inside it and draw no handles, so the toolbar/tooltip region margins never show half of either.
        Brush(0xff248dff);
        if(state.clear_outside)target_->DrawRectangle(Rect(Box{b.left+.75f,b.top+.75f,b.right-.75f,b.bottom-.75f}),brush_.Get(),1.5f);
        else target_->DrawRectangle(Rect(b),brush_.Get(),1.5f);
        if(state.selected&&!state.clear_outside) {
            for(const Point p : {Point{b.left,b.top},Point{(b.left+b.right)/2,b.top},Point{b.right,b.top},
                Point{b.left,(b.top+b.bottom)/2},Point{b.right,(b.top+b.bottom)/2},
                Point{b.left,b.bottom},Point{(b.left+b.right)/2,b.bottom},Point{b.right,b.bottom}}) {
                Brush(0xfff9fcff);target_->FillRectangle({p.x-3,p.y-3,p.x+3,p.y+3},brush_.Get());
                Brush(0xff248dff);target_->DrawRectangle({p.x-3,p.y-3,p.x+3,p.y+3},brush_.Get());
            }
        }
        if(!state.clear_outside) {  // size chip: meaningless over a pin, and it could peek into a toolbar margin
            const float scale=state.toolbar.scale;
            const float chip_y=std::max(float(monitor.top)+6,b.top-30*scale);
            Panel({b.left,chip_y,b.left+106*scale,chip_y+25*scale},acrylic_bitmap_.Get(),frame,true,6*scale);
            Text(std::to_wstring(int(b.right-b.left))+L" × "+std::to_wstring(int(b.bottom-b.top)),
                {b.left+8*scale,chip_y+3*scale,b.left+104*scale,chip_y+25*scale},13*scale,0xfff5faff);
        }
    }
    // No resize handles while typing: only the dashed edit box is active.
    if(editing_text<0&&!state.text_edit.mark&&document.selected>=0 && static_cast<size_t>(document.selected)<document.marks.size()) {
        DrawSelectionEditor(target_.Get(),document.marks[document.selected],document.selected_part,state.toolbar.scale);
    }
    if(state.selected)Toolbar(frame,document,state);
    if(state.selected&&state.tool==Tool::Mosaic&&Contains(state.selection,state.pointer)&&
        !Contains(state.toolbar,state.pointer)) {
        const auto circle=D2D1::Ellipse(Pt(state.pointer),state.mosaic_brush*state.toolbar.scale/2,state.mosaic_brush*state.toolbar.scale/2);
        Brush(0xbb142238);target_->DrawEllipse(circle,brush_.Get(),3);
        Brush(0xffeff7ff);target_->DrawEllipse(circle,brush_.Get(),1);
    }
    const float s=state.toolbar.scale;
    Box hint{float(monitor.left)+20*s,float(monitor.bottom)-46*s,
        float(monitor.right)-20*s,float(monitor.bottom)-14*s};
    // Reserve the toolbar and its tooltip area independently of hover state.
    const Box toolbar_extent=state.toolbar.Extent();
    if(state.selected && toolbar_extent.bottom+6*s>hint.top &&
        toolbar_extent.top-6*s<hint.bottom) {
        hint.right=std::min(hint.right,toolbar_extent.left-12*s);
    }
    if(hint.right-hint.left>=160*s)
        Text(state.busy?L"正在生成图片…  Esc 取消":state.hint,hint,13*s,0xfff2f6fc);
    if(!state.selected && !state.dragging && !state.external_magnifier) {
        const float mx=std::clamp(state.pointer.x+22, float(monitor.left), float(monitor.right)-130);
        const float my=std::clamp(state.pointer.y+24, float(monitor.top), float(monitor.bottom)-150);
        const Box zoom{mx,my,mx+120,my+120};
        const float sx=std::clamp(state.pointer.x-float(monitor.left)-10,0.0f,std::max(0.0f,float(monitor.right-monitor.left)-20));
        const float sy=std::clamp(state.pointer.y-float(monitor.top)-10,0.0f,std::max(0.0f,float(monitor.bottom-monitor.top)-20));
        if(state.pointer.x>=monitor.left && state.pointer.x<monitor.right && state.pointer.y>=monitor.top && state.pointer.y<monitor.bottom) {
            target_->DrawBitmap(screen_bitmap_.Get(),Rect(zoom),1,D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR,{sx,sy,sx+20,sy+20});
            Brush(0xff1686ff);target_->DrawRectangle(Rect(zoom),brush_.Get(),1);
            target_->DrawLine({mx+60,my},{mx+60,my+120},brush_.Get());target_->DrawLine({mx,my+60},{mx+120,my+60},brush_.Get());
        }
    }
}

void Renderer::ResetTarget() {
    for(auto& cache:mosaic_cache_)cache.bitmap.Reset();
    screen_bitmap_.Reset();acrylic_bitmap_.Reset();color_wheel_bitmap_.Reset();brush_.Reset();target_.Reset();window_surface_.Reset();
}

void Renderer::Prepare(HWND window,RECT monitor) {
    if(!window_surface_.Target()) {
        TraceScope initialize("surface_initialize",IsWindowVisible(window)?1:0);
        window_surface_.Create(window,static_cast<UINT>(monitor.right-monitor.left),static_cast<UINT>(monitor.bottom-monitor.top),factory_.Get());
        for(auto& cache:mosaic_cache_)cache.bitmap.Reset();
        target_=window_surface_.Target();monitor_=monitor;
    }
}

bool Renderer::Paint(HWND window, RECT monitor, const Frame& frame, const Frame& acrylic,
    const Document& document, const std::optional<Mark>& draft, const ViewState& state,int editing_text) {
    TraceScope trace("paint",state.dragging?1:(state.selected?2:0));
    Prepare(window,monitor);
    if(!screen_bitmap_) {
        TraceScope initialize("render_initialize",IsWindowVisible(window)?1:0);
        screen_bitmap_=BitmapRegion(frame,monitor);acrylic_bitmap_=Bitmap(acrylic);
    }
    const bool hidden=!IsWindowVisible(window);
    if(!window_surface_.Acquire(hidden)){TraceScope deferred("frame_deferred");return false;}
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Translation(-float(monitor.left),-float(monitor.top)));
    {TraceScope commands("draw_commands");Chrome(frame,acrylic,document,draft,state,monitor,editing_text);Picker(frame,state);}
    const int mode=state.dragging?1:(state.selected?2:0);
    HRESULT hr{};{TraceScope draw("end_draw",mode);hr=target_->EndDraw();}
    if(SUCCEEDED(hr)){
        TraceScope present(hidden?"prepare_submit":"present",mode);hr=window_surface_.Present(hidden);
    }
    if(hr==D2DERR_RECREATE_TARGET||hr==DXGI_ERROR_DEVICE_REMOVED||hr==DXGI_ERROR_DEVICE_RESET) {
        ResetTarget();return false;
    }
    Check(hr);return true;
}

Frame Renderer::Flatten(const Frame& frame,const Document& document,RECT selection) {
    Frame output=Crop(frame,selection);
    ComPtr<IWICImagingFactory> wic;
    Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)));
    ComPtr<IWICBitmap> bitmap;
    Check(wic->CreateBitmap(output.Width(),output.Height(),GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap));
    for(auto& cache:mosaic_cache_)cache.bitmap.Reset();
    Check(factory_->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&target_));
    brush_.Reset();color_wheel_bitmap_.Reset();
    auto source=Bitmap(output);
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Translation(-float(output.bounds.left),-float(output.bounds.top)));
    target_->DrawBitmap(source.Get(),Rect(BoxOf(output.bounds)),1,D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    Marks(frame,document,std::nullopt);
    Check(target_->EndDraw());
    Check(bitmap->CopyPixels(nullptr,output.Width()*4,static_cast<UINT>(output.pixels.size()*4),
        reinterpret_cast<BYTE*>(output.pixels.data())));
    return output;
}

Frame Renderer::Canvas(const Frame& frame,const Document& document,const ViewState& state,int editing_text) {
    Frame output=MakeFrame(frame.bounds);
    ComPtr<IWICImagingFactory> wic;
    Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)));
    ComPtr<IWICBitmap> bitmap;
    Check(wic->CreateBitmap(output.Width(),output.Height(),GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap));
    for(auto& cache:mosaic_cache_)cache.bitmap.Reset();
    brush_.Reset();color_wheel_bitmap_.Reset();target_.Reset();
    Check(factory_->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&target_));
    screen_bitmap_=Bitmap(frame);acrylic_bitmap_=Bitmap(frame);
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Translation(-float(frame.bounds.left),-float(frame.bounds.top)));
    Chrome(frame,frame,document,std::nullopt,state,frame.bounds,editing_text);
    Check(target_->EndDraw());
    Check(bitmap->CopyPixels(nullptr,output.Width()*4,static_cast<UINT>(output.pixels.size()*4),
        reinterpret_cast<BYTE*>(output.pixels.data())));
    return output;
}

// Software acrylic for the capture chrome (toolbar, property panels, chips):
// 4x area-average reduction, three 7-tap box passes per axis (close to a Gaussian,
// ~14 px at full scale), then saturation x1.3 as Fluent acrylic does so colours
// show through the tint. Integer-only so tests can pin exact pixels.
uint32_t AcrylicSaturate(unsigned r,unsigned g,unsigned b) {
    const int gray=static_cast<int>((r*77u+g*150u+b*29u)>>8);
    const auto boost=[gray](unsigned c){return static_cast<uint32_t>(std::clamp(gray+(static_cast<int>(c)-gray)*13/10,0,255));};
    return 0xff000000u|(boost(r)<<16)|(boost(g)<<8)|boost(b);
}
Frame BlurBackdrop(const Frame& frame) {
    // On the capture start path (the overlay waits for it), so: R and B are summed together in
    // one 32-bit word (lanes 0x00RR00BB, at most 16 x 255 per lane), and vertical passes walk
    // rows with per-column running sums instead of striding down columns.
    const int fw=frame.Width(),fh=frame.Height();
    const int w=std::max(1,fw/4),h=std::max(1,fh/4);
    Frame reduced=MakeFrame({0,0,w,h});
    if(fw<=0||fh<=0||frame.pixels.size()<static_cast<size_t>(fw)*static_cast<size_t>(fh))return reduced;
    constexpr uint32_t Lanes=0x00ff00ffu;
    auto& px=reduced.pixels;
    const size_t width=static_cast<size_t>(w);
    std::vector<uint32_t> rbs(width),gs(width);
    for(int y=0;y<h;++y) {
        std::fill(rbs.begin(),rbs.end(),0u);std::fill(gs.begin(),gs.end(),0u);
        for(int dy=0;dy<4;++dy) {
            const uint32_t* row=frame.pixels.data()+static_cast<size_t>(std::min(y*4+dy,fh-1))*static_cast<size_t>(fw);
            if(fw>=4) {
                for(size_t x=0;x<width;++x){const uint32_t* q=row+x*4;
                    rbs[x]+=(q[0]&Lanes)+(q[1]&Lanes)+(q[2]&Lanes)+(q[3]&Lanes);
                    gs[x]+=((q[0]>>8)&255u)+((q[1]>>8)&255u)+((q[2]>>8)&255u)+((q[3]>>8)&255u);}
            } else {
                for(size_t x=0;x<width;++x)for(int dx=0;dx<4;++dx){const uint32_t v=row[std::min(static_cast<int>(x)*4+dx,fw-1)];rbs[x]+=v&Lanes;gs[x]+=(v>>8)&255u;}
            }
        }
        uint32_t* dst=px.data()+static_cast<size_t>(y)*width;
        for(size_t x=0;x<width;++x)dst[x]=0xff000000u|((((rbs[x]>>16)+8)>>4)<<16)|(((gs[x]+8)>>4)<<8)|(((rbs[x]&0xffffu)+8)>>4);
    }
    const auto box=[](uint32_t rb,uint32_t g){return 0xff000000u|((((rb>>16)+3)/7)<<16)|(((g+3)/7)<<8)|(((rb&0xffffu)+3)/7);};
    std::vector<uint32_t> scratch(px.size());
    for(int round=0;round<3;++round) {
        for(int y=0;y<h;++y) {  // horizontal: px -> scratch
            const uint32_t* row=px.data()+static_cast<size_t>(y)*width;uint32_t* dst=scratch.data()+static_cast<size_t>(y)*width;
            const auto at=[&](int x){return row[std::clamp(x,0,w-1)];};
            uint32_t rb{},g{};
            for(int d=-3;d<=3;++d){const uint32_t v=at(d);rb+=v&Lanes;g+=(v>>8)&255u;}
            for(int x=0;x<w;++x){
                dst[x]=box(rb,g);
                const uint32_t gone=at(x-3),next=at(x+4);
                rb+=(next&Lanes)-(gone&Lanes);g+=((next>>8)&255u)-((gone>>8)&255u);  // exact modulo 2^32; lanes stay in range
            }
        }
        // vertical: scratch -> px, row-major with per-column sums
        const auto line=[&](int y){return scratch.data()+static_cast<size_t>(std::clamp(y,0,h-1))*width;};
        std::fill(rbs.begin(),rbs.end(),0u);std::fill(gs.begin(),gs.end(),0u);
        for(int d=-3;d<=3;++d){const uint32_t* src=line(d);for(size_t x=0;x<width;++x){rbs[x]+=src[x]&Lanes;gs[x]+=(src[x]>>8)&255u;}}
        for(int y=0;y<h;++y){
            uint32_t* dst=px.data()+static_cast<size_t>(y)*width;
            for(size_t x=0;x<width;++x)dst[x]=box(rbs[x],gs[x]);
            const uint32_t* gone=line(y-3);const uint32_t* next=line(y+4);
            for(size_t x=0;x<width;++x){rbs[x]+=(next[x]&Lanes)-(gone[x]&Lanes);gs[x]+=((next[x]>>8)&255u)-((gone[x]>>8)&255u);}
        }
    }
    for(auto& v:px)v=AcrylicSaturate((v>>16)&255,(v>>8)&255,v&255);
    return reduced;
}

Frame Renderer::Demo(bool chrome, bool dark, Tool preview_tool,bool color_picker,int preview_hover,int preview_dropdown,bool highlighter,const ToolbarCustomization* customization) {
    Frame frame=MakeFrame({0,0,1280,800});
    for(int y=0;y<800;++y)for(int x=0;x<1280;++x) {
        const float dx=(float(x)-1100)/450,dy=(float(y)-540)/330;
        const float warm=std::exp(-dx*dx-dy*dy);
        const float wave=std::sin(float(x)/230+float(y)/210)*8;
        const auto r=static_cast<uint32_t>(std::clamp(28+warm*155+wave,0.0f,255.0f));
        const auto g=static_cast<uint32_t>(std::clamp(61+warm*60+wave,0.0f,255.0f));
        const auto b=static_cast<uint32_t>(std::clamp(90+warm*15+wave,0.0f,255.0f));
        frame.pixels[static_cast<size_t>(y)*1280+x]=0xff000000|(r<<16)|(g<<8)|b;
    }
    ComPtr<IWICImagingFactory> wic;
    Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)));
    ComPtr<IWICBitmap> bitmap;
    const auto prepare=[&]() {
        brush_.Reset();color_wheel_bitmap_.Reset();target_.Reset();bitmap.Reset();
        Check(wic->CreateBitmap(1280,800,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap));
        for(auto& cache:mosaic_cache_)cache.bitmap.Reset();
        Check(factory_->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&target_));
    };
    prepare();auto backdrop=Bitmap(frame);
    target_->BeginDraw();target_->DrawBitmap(backdrop.Get());
    Brush(0xfffafbfe);target_->FillRectangle({180,90,1110,600},brush_.Get());
    Brush(0xffeff2f7);target_->FillRectangle({180,90,1110,138},brush_.Get());
    Text(L"项目笔记.txt",{204,104,600,130},15,0xff253344);
    Text(L"—       □       ×",{981,103,1095,132},17,0xff4e5c6c);
    Text(L"项目笔记",{224,174,1050,226},34,0xff182432);
    Text(L"LumaShot  ·  轻巧地记录每一个细节",{226,229,1050,262},15,0xff7a8698);
    Brush(0xffdce3ed);target_->DrawLine({226,278},{1064,278},brush_.Get());
    Text(L"梳理产品核心流程，聚焦截图与标注的极简体验。",{226,309,1060,344},19,0xff243142);
    Text(L"需要进一步优化启动速度和内存占用，保持轻量。",{226,353,830,388},19,0xff243142);
    Text(L"待办事项",{226,420,1000,450},19,0xff243142);
    Text(L"✓  完善核心截图功能\n□  保留鼠标指针的实际形态\n□  优化标注工具的交互细节",{226,456,1000,548},18,0xff354357);
    Text(L"参考资料： internal-notes.example",{226,560,1000,590},16,0xff64748b);
    Check(target_->EndDraw());
    Check(bitmap->CopyPixels(nullptr,1280*4,static_cast<UINT>(frame.pixels.size()*4),reinterpret_cast<BYTE*>(frame.pixels.data())));
    auto cursor=FrozenCursor::Copy(LoadCursorW(nullptr,IDC_HAND),{520,477});cursor.Composite(frame);
    if(!chrome)return frame;
    auto acrylic=BlurBackdrop(frame);Document document;
    Mark box;box.a={219,350};box.b={805,388};box.width=2;document.Add(box);
    Mark arrow;arrow.tool=Tool::Arrow;arrow.a={880,352};arrow.b={816,367};arrow.width=3;document.Add(arrow);
    Mark note;note.tool=Tool::Text;note.a={889,335};note.b={1090,373};note.text=L"这里需要调整";note.font_size=20;document.Add(note);
    Mark mosaic;mosaic.tool=Tool::Mosaic;mosaic.a={307,561};mosaic.b={603,583};mosaic.width=3;document.Add(mosaic);
    ViewState state;state.dark=dark;state.selected=true;state.selection={180,90,1110,600};state.tool=preview_tool;state.width=2;state.hover=preview_hover;
    if(highlighter){
        state.pen_mode=PenMode::Highlighter;document.Reset();
        Mark pen;pen.tool=Tool::Pen;pen.pen_mode=PenMode::Highlighter;pen.pen_opacity=.4f;pen.width=24;pen.color=0xffffff00;
        pen.points={{397,322},{632,322}};document.Add(pen);pen.points={{225,366},{538,366}};document.Add(pen);
        pen.color=0xffff68c8;pen.points={{247,518},{442,518}};document.Add(pen);
    }
    if(preview_dropdown==70||preview_dropdown==71)state.number_combo=NumberCombo::Text;
    state.toolbar=PlaceToolbar(state.selection,frame.bounds,1,preview_tool==Tool::Select||(customization&&!customization->auto_properties)?0.f:1.f,preview_tool,state.number_combo,customization?*customization:ToolbarCustomization{});state.hint=L"已包含鼠标指针   ·   Enter / Ctrl+C 复制   ·   Ctrl+S 保存   ·   Esc 取消";
    if(color_picker){state.color=0xffff4d4f;state.picker.Open(state.color,state.toolbar.Property(28),frame.bounds,1);}
    if(preview_dropdown==70||preview_dropdown==71)state.number_combo=NumberCombo::Text;
    if(preview_dropdown==ToolbarMoreId)OpenToolbarMoreMenu(state,frame.bounds);
    else if(preview_dropdown==ToolbarContextMenuId){
        int target=IsCustomizableToolbarId(preview_hover)?preview_hover:-1;
        for(int id:state.toolbar.custom.order)if(target<0&&state.toolbar.custom.OnPanel(id))target=id;
        OpenToolbarContextMenu(state,target,frame.bounds);
    }
    else if(preview_dropdown>=0)OpenToolbarDropdown(state,preview_dropdown,frame.bounds);
    prepare();screen_bitmap_=Bitmap(frame);acrylic_bitmap_=Bitmap(acrylic);
    target_->BeginDraw();Chrome(frame,acrylic,document,std::nullopt,state,frame.bounds);Picker(frame,state);Check(target_->EndDraw());
    Check(bitmap->CopyPixels(nullptr,1280*4,static_cast<UINT>(frame.pixels.size()*4),reinterpret_cast<BYTE*>(frame.pixels.data())));
    return frame;
}

}
