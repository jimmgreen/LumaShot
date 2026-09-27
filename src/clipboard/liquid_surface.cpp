#include "clipboard/liquid_surface.h"
#include "clipboard/liquid_neck.h"
#include <d2d1helper.h>
#include <wrl/client.h>
#include <stdexcept>
#include <vector>
#include <cstring>

namespace lumashot::clipboard {
using Microsoft::WRL::ComPtr;
namespace {
void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Liquid clipboard surface failed");}
D2D1_COLOR_F Color(UINT32 value,float alpha=1){return D2D1::ColorF(value,alpha);}
ComPtr<ID2D1Geometry> Shape(ID2D1Factory* factory,const LiquidFrame& frame) {
    const auto& b=frame.pose.bounds;const float scale=frame.scale;
    const float cx=(b.left+b.right)/2-frame.viewport.left,cy=(b.top+b.bottom)/2-frame.viewport.top;
    const float width=b.right-b.left,height=b.bottom-b.top;
    const float sx=1+std::min(.19f,std::abs(frame.pull.x)*.025f);
    const float sy=1+std::min(.12f,std::abs(frame.pull.y)*.018f)-std::min(.04f,std::abs(frame.pull.x)*.006f);
    const float end_radius=frame.expanded_radius_pixels>=0?frame.expanded_radius_pixels:8*scale;
    const float radius=frame.native_clip?0.f:std::min({width/2,height/2,
        8*scale+(end_radius-8*scale)*LiquidSmooth(frame.pose.open)});
    const D2D1_RECT_F rect{cx-width/2,cy-height/2,cx+width/2,cy+height/2};
    ComPtr<ID2D1RoundedRectangleGeometry> rounded;
    Check(factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(rect,radius,radius),&rounded));
    const float shear=frame.pull.x*scale/std::max(height,1.f)*.45f;
    const auto transform=D2D1::Matrix3x2F(sx,0,shear,sy,cx*(1-sx)-cy*shear-frame.pull.x*scale*.18f,
        cy*(1-sy)-frame.pull.y*scale*.12f);
    ComPtr<ID2D1TransformedGeometry> body;
    Check(factory->CreateTransformedGeometry(rounded.Get(),&transform,&body));
    auto dock=frame.dock?FindLiquidDock(b,frame.work,scale):LiquidDock{};
    dock.strength*=1-LiquidSmooth(frame.pose.open/.18f);
    if(dock.strength<=.001f)return body;
    // Resting shoulders morph into an hourglass neck; its waist and both
    // attachment ports shrink together before rupture. The body's port follows
    // its deformation while the screen port stays fixed at the work boundary.
    const bool horizontal=dock.edge==LiquidEdge::Left||dock.edge==LiquidEdge::Right;
    const bool negative=dock.edge==LiquidEdge::Left||dock.edge==LiquidEdge::Top;
    const float half=(horizontal?height:width)/2;
    const float radial=(horizontal?width:height)/2;
    const float gap=std::max(0.f,LiquidEdgeGap(b,frame.work,dock.edge));
    const auto profile=LiquidNeckProfile(radial,half,std::min(radius,half),gap,scale);
    const float edge=radial+gap,fade=1-LiquidSmooth(frame.pose.open/.18f);
    const auto point=[&](size_t i,float side){
        const auto v=profile[i];const float x=(negative?-1.f:1.f)*v.x,y=side*v.y*fade;
        const auto p=horizontal?D2D1::Point2F(cx+x,cy+y):D2D1::Point2F(cx+y,cy+x);
        const auto moved=transform.TransformPoint(p);
        const float follow=std::clamp((edge-v.x)/std::max(.01f,edge-profile[0].x),0.f,1.f);
        return D2D1::Point2F(p.x+(moved.x-p.x)*follow,p.y+(moved.y-p.y)*follow);
    };
    ComPtr<ID2D1PathGeometry> bridge;Check(factory->CreatePathGeometry(&bridge));
    ComPtr<ID2D1GeometrySink> sink;Check(bridge->Open(&sink));
    sink->BeginFigure(point(0,1),D2D1_FIGURE_BEGIN_FILLED);
    sink->AddBezier(D2D1::BezierSegment(point(1,1),point(2,1),point(3,1)));
    sink->AddBezier(D2D1::BezierSegment(point(4,1),point(5,1),point(6,1)));
    sink->AddLine(point(6,-1));
    sink->AddBezier(D2D1::BezierSegment(point(5,-1),point(4,-1),point(3,-1)));
    sink->AddBezier(D2D1::BezierSegment(point(2,-1),point(1,-1),point(0,-1)));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);Check(sink->Close());
    ComPtr<ID2D1PathGeometry> joined;Check(factory->CreatePathGeometry(&joined));sink.Reset();Check(joined->Open(&sink));
    Check(body->CombineWithGeometry(bridge.Get(),D2D1_COMBINE_MODE_UNION,nullptr,.15f,sink.Get()));Check(sink->Close());
    return joined;
}
}
struct LiquidSurface::Impl {
    ComPtr<ID2D1Factory> factory;
    ComPtr<ID2D1DCRenderTarget> target;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<ID2D1Layer> layer;
    ComPtr<ID2D1StrokeStyle> stroke;
    ComPtr<ID2D1Bitmap> compact,expanded;
    ComPtr<ID2D1Geometry> geometry;
    std::unique_ptr<DibSurface> surface;
    int width{},height{};
    Impl(){
        Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf()));
        const auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
        Check(factory->CreateDCRenderTarget(&props,&target));
        Check(target->CreateSolidColorBrush(Color(0),&brush));Check(target->CreateLayer(nullptr,&layer));
        auto style=D2D1::StrokeStyleProperties();style.lineJoin=D2D1_LINE_JOIN_ROUND;
        // Dock flares meet the work-area boundary tangentially. Miter joins
        // amplify that cusp into a long spike; round joins keep the halo bounded.
        Check(factory->CreateStrokeStyle(style,nullptr,0,&stroke));
    }
    void Cache(bool full,int w,int h,const uint32_t* pixels){
        auto& bitmap=full?expanded:compact;bitmap.Reset();
        const auto props=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        Check(target->CreateBitmap(D2D1::SizeU(static_cast<UINT>(w),static_cast<UINT>(h)),pixels,static_cast<UINT>(w)*4,props,&bitmap));
    }
    void Render(const LiquidFrame& frame){
        const int w=frame.viewport.right-frame.viewport.left,h=frame.viewport.bottom-frame.viewport.top;
        if(w<=0||h<=0||static_cast<long long>(w)*h>3600000)throw std::runtime_error("Liquid frame exceeds its bounded viewport");
        if(!surface||width!=w||height!=h){surface=std::make_unique<DibSurface>(w,h);width=w;height=h;}
        const RECT client{0,0,w,h};Check(target->BindDC(surface->Dc(),&client));target->SetDpi(96,96);
        geometry=Shape(factory.Get(),frame);
        target->BeginDraw();target->SetTransform(D2D1::Matrix3x2F::Identity());target->Clear(Color(0,0));
        // Small analytic halo, not a sampled/blurred copy of the desktop.
        brush->SetColor(Color(0x0a192e,.018f*frame.detail));target->DrawGeometry(geometry.Get(),brush.Get(),7*frame.scale,stroke.Get());
        brush->SetColor(Color(0x0a192e,.035f*frame.detail));target->DrawGeometry(geometry.Get(),brush.Get(),3*frame.scale,stroke.Get());
        brush->SetColor(Color(frame.dark?0x192330:0xf5f8fc,frame.opacity));target->FillGeometry(geometry.Get(),brush.Get());
        brush->SetColor(Color(frame.dark?0x8da7c5:0xffffff,.22f*frame.detail));target->DrawGeometry(geometry.Get(),brush.Get(),frame.scale,stroke.Get());
        target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(),geometry.Get()),layer.Get());
        const auto& b=frame.pose.bounds;
        const float left=b.left-frame.viewport.left,top=b.top-frame.viewport.top;
        const float right=b.right-frame.viewport.left,bottom=b.bottom-frame.viewport.top;
        const float full_alpha=LiquidSmooth((frame.pose.open-.62f)/.38f);
        const float compact_alpha=1-LiquidSmooth(frame.pose.open/.22f);
        if(expanded&&full_alpha>0){
            const auto size=expanded->GetSize();const float y=top+(1-frame.pose.open)*8*frame.scale;
            // Rigid pixels: reveal/translate the cached text, never stretch its glyphs.
            target->DrawBitmap(expanded.Get(),D2D1::RectF(right-size.width,y,right,y+size.height),full_alpha);
        }
        if(compact&&compact_alpha>0){
            const auto size=compact->GetSize();const float x=(left+right-size.width)/2-frame.pull.x*frame.scale*.18f;
            const float y=(top+bottom-size.height)/2-frame.pull.y*frame.scale*.12f;
            target->DrawBitmap(compact.Get(),D2D1::RectF(x,y,x+size.width,y+size.height),compact_alpha);
        }
        target->PopLayer();Check(target->EndDraw());
    }
};
LiquidSurface::LiquidSurface():impl_(std::make_unique<Impl>()){}
LiquidSurface::~LiquidSurface()=default;
void LiquidSurface::Cache(bool expanded,int w,int h,const uint32_t* pixels){impl_->Cache(expanded,w,h,pixels);}
void LiquidSurface::ClearExpanded(){impl_->expanded.Reset();}
void LiquidSurface::Render(const LiquidFrame& frame){impl_->Render(frame);}
bool LiquidSurface::Contains(POINT local)const{
    BOOL contains=FALSE;
    return impl_->geometry&&SUCCEEDED(impl_->geometry->FillContainsPoint(D2D1::Point2F(float(local.x),float(local.y)),nullptr,&contains))&&contains;
}
HRGN LiquidSurface::CreateInputRegion()const{
    // Include every nonzero-alpha edge/shadow pixel: this is a conservative
    // input mask, NOT an integer replacement for the antialiased vector outline.
    // Unlike HTTRANSPARENT alone, a native region also excludes other threads.
    std::vector<RECT> spans;
    for(int y=0;y<Height();++y){
        const auto* row=Pixels()+size_t(y)*Width();int left=0,right=Width();
        while(left<right&&(row[left]>>24)==0)++left;
        while(right>left&&(row[right-1]>>24)==0)--right;
        if(left==right)continue;
        if(!spans.empty()&&spans.back().left==left&&spans.back().right==right&&spans.back().bottom==y)spans.back().bottom=y+1;
        else spans.push_back({left,y,right,y+1});
    }
    if(spans.empty())return CreateRectRgn(0,0,0,0);
    const size_t bytes=sizeof(RGNDATAHEADER)+spans.size()*sizeof(RECT);
    std::vector<unsigned char> storage(bytes);auto* data=reinterpret_cast<RGNDATA*>(storage.data());
    data->rdh={sizeof(RGNDATAHEADER),RDH_RECTANGLES,static_cast<DWORD>(spans.size()),static_cast<DWORD>(spans.size()*sizeof(RECT)),{0,0,Width(),Height()}};
    std::memcpy(data->Buffer,spans.data(),spans.size()*sizeof(RECT));
    return ExtCreateRegion(nullptr,static_cast<DWORD>(bytes),data);
}
const uint32_t* LiquidSurface::Pixels()const{return impl_->surface?impl_->surface->Pixels():nullptr;}
int LiquidSurface::Width()const{return impl_->width;}
int LiquidSurface::Height()const{return impl_->height;}
}
