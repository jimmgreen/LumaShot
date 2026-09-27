#include "pin/selection_tools.h"
#include "ui/render.h"
#include "export/png.h"
#include <wincodec.h>
#include <iostream>
using namespace lumashot;
int main(){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int failures=0;
    auto expect=[&](bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<'\n';if(!ok)++failures;};
    ocr::Text text;ocr::Line line;line.glyphs={ {L"A",{10,10,20,30},1},{L"e\u0301",{24,10,34,30},1} };text.lines.push_back(line);line.glyphs={{L"中",{10,40,30,60},1}};text.lines.push_back(line);
    auto all=ocr::All(text);const auto boxes=SelectionBoxes(text,all);expect(boxes.size()==2&&boxes[0].right==34,"continuous highlight joins glyph gaps per line");
    std::swap(all.anchor,all.caret);expect(SelectionBoxes(text,all).size()==2&&ocr::Selected(text,all)==L"Ae\u0301\r\n中","reverse multiline selection preserves graphemes");
    auto handles=TextHandles(text,all,1);expect(HitTextHandle(handles,handles.start,1)==0&&HitTextHandle(handles,handles.end,1)==1,"reverse selection handles use reading order");
    for(float dpi:{1.f,1.5f,2.f})for(RECT work:{RECT{-1280,-100,0,900},RECT{0,0,320,200}}){auto bar=PlaceSelectionBar({float(work.right-80),float(work.bottom-30),float(work.right),float(work.bottom)},work,dpi);expect(bar.bounds.left>=work.left&&bar.bounds.right<=work.right&&bar.bounds.top>=work.top&&bar.bounds.bottom<=work.bottom&&bar.above,"toolbar stays on screen at negative origin and mixed DPI");}
    try{
        Frame frame=MakeFrame({0,0,760,430},0xffedf4ff);Document doc;Mark mark;mark.tool=Tool::Text;mark.color=0xff183451;mark.font_size=36;mark.a={60,34};mark.b={690,95};mark.text=L"项目笔记";doc.Add(mark);
        mark.font_size=19;mark.a={62,103};mark.b={690,140};mark.text=L"LumaShot · 轻巧地记录每一个细节";doc.Add(mark);
        mark.font_size=23;mark.a={62,171};mark.b={720,211};mark.text=L"梳理产品核心流程，聚焦截图与标注的极简体验。";doc.Add(mark);
        mark.a={62,220};mark.b={720,260};mark.text=L"需要进一步优化启动速度和内存占用，保持轻量。";doc.Add(mark);
        Renderer renderer;frame=renderer.Flatten(frame,doc,frame.bounds);
        ComPtr<IWICImagingFactory> wic;CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic));ComPtr<IWICBitmap> bitmap;
        wic->CreateBitmapFromMemory(frame.Width(),frame.Height(),GUID_WICPixelFormat32bppPBGRA,frame.Width()*4,static_cast<UINT>(frame.pixels.size()*4),reinterpret_cast<BYTE*>(frame.pixels.data()),&bitmap);
        ComPtr<ID2D1Factory> factory;D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf());ComPtr<ID2D1RenderTarget> target;
        factory->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&target);
        target->BeginDraw();ComPtr<ID2D1SolidColorBrush> brush;target->CreateSolidColorBrush(D2D1::ColorF(0x3388ff,.32f),&brush);target->FillRectangle({62,221,499,250},brush.Get());
        DrawTextHandles(target.Get(),{{62,216},{499,255},true},1);
        target->SetTransform(D2D1::Matrix3x2F::Translation(62,278));DrawSelectionBar(target.Get(),600,120,1.5f,-1);target->EndDraw();
        bitmap->CopyPixels(nullptr,frame.Width()*4,static_cast<UINT>(frame.pixels.size()*4),reinterpret_cast<BYTE*>(frame.pixels.data()));SavePng(frame,L"selection-tools-preview.png");expect(true,"selection and toolbar render with production drawing functions");
    }catch(const std::exception& e){std::cout<<e.what()<<'\n';++failures;}
    CoUninitialize();return failures?1:0;
}
