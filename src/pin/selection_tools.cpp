#include "pin/selection_tools.h"
#include "ui/text_renderer.h"
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
namespace lumashot {
using Microsoft::WRL::ComPtr;
std::vector<Box> SelectionBoxes(const ocr::Text& text,ocr::Selection s){
    if(s.caret<s.anchor)std::swap(s.anchor,s.caret);std::vector<Box> result;
    for(size_t l=s.anchor.line;l<text.lines.size()&&l<=s.caret.line;++l){const auto& glyphs=text.lines[l].glyphs;const size_t begin=l==s.anchor.line?s.anchor.glyph:0,end=l==s.caret.line?std::min(s.caret.glyph,glyphs.size()):glyphs.size();
        if(begin>=end)continue;Box box=glyphs[begin].box;for(size_t g=begin+1;g<end;++g){const auto b=glyphs[g].box;box.left=std::min(box.left,b.left);box.top=std::min(box.top,b.top);box.right=std::max(box.right,b.right);box.bottom=std::max(box.bottom,b.bottom);}result.push_back(box);
    }return result;
}
SelectionHandles TextHandles(const ocr::Text& text,ocr::Selection selection,float scale){
    const auto boxes=SelectionBoxes(text,selection);if(boxes.empty())return {};
    return {{boxes.front().left,boxes.front().top-5*scale},{boxes.back().right,boxes.back().bottom+5*scale},true};
}
int HitTextHandle(SelectionHandles h,Point point,float scale){
    if(!h.visible)return -1;
    if(std::hypot(point.x-h.start.x,point.y-h.start.y)<=10*scale)return 0;
    if(std::hypot(point.x-h.end.x,point.y-h.end.y)<=10*scale)return 1;return -1;
}
SelectionBar PlaceSelectionBar(Box selected,RECT work,float scale){
    scale=std::min({scale,float(work.right-work.left-16)/400,float(work.bottom-work.top-16)/80});
    const float w=400*scale,h=80*scale;
    float x=std::clamp((selected.left+selected.right-w)/2,float(work.left)+8,std::max(float(work.left)+8,float(work.right)-w-8));
    float y=selected.bottom+18*scale;const bool above=y+h>work.bottom-8;
    if(above)y=selected.top-18*scale-h;
    y=std::clamp(y,float(work.top)+8,std::max(float(work.top)+8,float(work.bottom)-h-8));
    return {{LONG(std::floor(x)),LONG(std::floor(y)),LONG(std::ceil(x+w)),LONG(std::ceil(y+h))},scale,above};
}
void DrawTextMarks(ID2D1RenderTarget* target,const std::vector<SelectionMark>& marks){
    ComPtr<ID2D1SolidColorBrush> brush;target->CreateSolidColorBrush(D2D1::ColorF(0xffdf42,.4f),&brush);
    for(const auto& mark:marks)for(auto b:mark.boxes){
        const float stroke=std::clamp((b.bottom-b.top)/14,1.5f,3.f);
        if(mark.kind==TextDecoration::Highlight){brush->SetColor(D2D1::ColorF(0xffdf42,.4f));target->FillRectangle({b.left,b.top,b.right,b.bottom},brush.Get());continue;}
        brush->SetColor(D2D1::ColorF(mark.kind==TextDecoration::Wave?0xf43f67:0x183451));
        const float y=mark.kind==TextDecoration::Strike?(b.top+b.bottom)/2:b.bottom-1;
        if(mark.kind!=TextDecoration::Wave)target->DrawLine({b.left,y},{b.right,y},brush.Get(),stroke);
        else {Point prev{b.left,y};for(float x=b.left+1;x<=b.right+1;x+=1){x=std::min(x,b.right);Point next{x,y+std::sin((x-b.left)*.7f)*stroke};target->DrawLine({prev.x,prev.y},{next.x,next.y},brush.Get(),stroke);prev=next;if(x==b.right)break;}}
    }
}
void DrawTextHandles(ID2D1RenderTarget* target,SelectionHandles h,float scale){
    if(!h.visible)return;ComPtr<ID2D1SolidColorBrush> brush;target->CreateSolidColorBrush(D2D1::ColorF(0x087cff),&brush);
    for(int i=0;i<2;++i){const auto p=i?h.end:h.start;target->FillEllipse(D2D1::Ellipse({p.x,p.y},5.5f*scale,5.5f*scale),brush.Get());target->DrawLine({p.x,p.y},{p.x,p.y+(i?-16:16)*scale},brush.Get(),1.7f*scale);}
}
void DrawSelectionBar(ID2D1RenderTarget* target,float width,float height,float scale,int hover,bool above,bool copied,int down){
    ComPtr<ID2D1SolidColorBrush> brush;target->CreateSolidColorBrush(D2D1::ColorF(0xf4f8ff),&brush);
    const float top=above?.5f:8*scale,bottom=above?height-8*scale:height-.5f,offset=above?-3*scale:3*scale;
    const auto panel=D2D1::RoundedRect({.5f,top,width-.5f,bottom},14*scale,14*scale);
    target->FillRoundedRectangle(panel,brush.Get());
    brush->SetColor(D2D1::ColorF(0xd4e0f1));target->DrawRoundedRectangle(panel,brush.Get());
    ComPtr<ID2D1Factory> shapes;target->GetFactory(&shapes);ComPtr<ID2D1PathGeometry> tip;shapes->CreatePathGeometry(&tip);ComPtr<ID2D1GeometrySink> tip_sink;tip->Open(&tip_sink);
    const float edge=above?bottom:top,apex=above?height-1:1;
    tip_sink->BeginFigure({width/2-8*scale,edge},D2D1_FIGURE_BEGIN_FILLED);tip_sink->AddLine({width/2,apex});tip_sink->AddLine({width/2+8*scale,edge});tip_sink->EndFigure(D2D1_FIGURE_END_CLOSED);tip_sink->Close();
    brush->SetColor(D2D1::ColorF(0xf4f8ff));target->FillGeometry(tip.Get(),brush.Get());brush->SetColor(D2D1::ColorF(0xd4e0f1));target->DrawLine({width/2-8*scale,edge},{width/2,apex},brush.Get());target->DrawLine({width/2,apex},{width/2+8*scale,edge},brush.Get());
    ComPtr<IDWriteFactory> factory;DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(factory.GetAddressOf()));
    ComPtr<IDWriteTextFormat> format;factory->CreateTextFormat(L"Microsoft YaHei UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,13*scale,L"zh-CN",&format);format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    constexpr LPCWSTR labels[]={L"复制",L"荧光笔",L"波浪线",L"直线",L"删除线"};
    thread_local TextRenderer text_renderer;
    for(int i=0;i<5;++i){
        const float l=i*width/5,r=(i+1)*width/5,c=(l+r)/2,y=27*scale+offset;
        if(i==hover||i==down||(i==0&&copied)){brush->SetColor(D2D1::ColorF(i==down?0xc6dcfa:(i==0&&copied?0xd9f3e5:0xe1edff)));target->FillRoundedRectangle(D2D1::RoundedRect({l+5*scale,7*scale,r-5*scale,height-7*scale},8*scale,8*scale),brush.Get());}
        if(i){brush->SetColor(D2D1::ColorF(0xd6e1f1));target->DrawLine({l,18*scale},{l,height-18*scale},brush.Get());}
        brush->SetColor(D2D1::ColorF(0x183451));
        auto line=[&](float x1,float y1,float x2,float y2){target->DrawLine({c+x1*scale,y+y1*scale},{c+x2*scale,y+y2*scale},brush.Get(),2*scale);};
        if(i==0&&copied){brush->SetColor(D2D1::ColorF(0x16834a));line(-8,0,-2,6);line(-2,6,9,-7);}
        if(i==0&&!copied){target->DrawRoundedRectangle(D2D1::RoundedRect({c-6*scale,y-5*scale,c+6*scale,y+9*scale},2*scale,2*scale),brush.Get(),2*scale);line(-2,-9,10,-9);line(10,-9,10,5);}
        if(i==1){brush->SetColor(D2D1::ColorF(0xffd32a));target->DrawLine({c-7*scale,y+7*scale},{c+6*scale,y-6*scale},brush.Get(),9*scale);brush->SetColor(D2D1::ColorF(0xff526b));target->DrawLine({c+6*scale,y-6*scale},{c+10*scale,y-10*scale},brush.Get(),9*scale);}
        if(i==2){brush->SetColor(D2D1::ColorF(0xf43f67));for(int x=-12;x<12;++x)line(float(x),std::sin(float(x)*.65f)*3,float(x+1),std::sin(float(x+1)*.65f)*3);}
        if(i==3)line(-8,8,8,-8);
        if(i==4){ComPtr<ID2D1PathGeometry> path;shapes->CreatePathGeometry(&path);ComPtr<ID2D1GeometrySink> sink;path->Open(&sink);const auto p=[&](float x,float yy){return D2D1_POINT_2F{c+x*scale,y+yy*scale};};sink->BeginFigure(p(7,-7),D2D1_FIGURE_BEGIN_HOLLOW);sink->AddBezier(D2D1::BezierSegment(p(-8,-14),p(-12,-2),p(0,0)));sink->AddBezier(D2D1::BezierSegment(p(12,2),p(8,14),p(-7,7)));sink->EndFigure(D2D1_FIGURE_END_OPEN);sink->Close();target->DrawGeometry(path.Get(),brush.Get(),2*scale);line(-10,0,10,0);}
        text_renderer.Draw(target,factory.Get(),i==0&&copied?L"已复制":labels[i],format.Get(),{l,48*scale+offset,r,height},D2D1::ColorF(0x183451),false);
    }
}
Frame FlattenTextMarks(const Frame& image,const std::vector<SelectionMark>& marks){
    if(marks.empty())return image;Frame result=image;ComPtr<IWICImagingFactory> wic;
    CheckWin32(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic))),"Create mark export");
    ComPtr<IWICBitmap> bitmap;CheckWin32(SUCCEEDED(wic->CreateBitmapFromMemory(image.Width(),image.Height(),GUID_WICPixelFormat32bppPBGRA,image.Width()*4,static_cast<UINT>(image.pixels.size()*4),reinterpret_cast<BYTE*>(result.pixels.data()),&bitmap)),"Create marked image");
    ComPtr<ID2D1Factory> factory;D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf());ComPtr<ID2D1RenderTarget> target;
    CheckWin32(SUCCEEDED(factory->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&target)),"Create mark renderer");
    target->BeginDraw();DrawTextMarks(target.Get(),marks);CheckWin32(SUCCEEDED(target->EndDraw()),"Draw image marks");
    CheckWin32(SUCCEEDED(bitmap->CopyPixels(nullptr,result.Width()*4,static_cast<UINT>(result.pixels.size()*4),reinterpret_cast<BYTE*>(result.pixels.data()))),"Read image marks");return result;
}
}

