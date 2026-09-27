// Production window and renderer exercised exclusively with synthetic image data.
#include "../src/pin/pin.cpp"
#include <iostream>
namespace lumashot {
struct PinTest {
    static int Run(){
        int failures=0;const auto expect=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';failures+=!ok;};
        HWND host=CreateWindowExW(0,L"STATIC",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
        {
            PinManager manager(host);PinStyle selected=DefaultPinStyle;manager.sticker_style=[&]{return selected;};
            auto image=MakeFrame({0,0,280,160},0xfff1f6ff);
            for(int y=0;y<image.Height();++y)for(int x=0;x<image.Width();++x)
                image.pixels[static_cast<size_t>(y)*image.Width()+x]=0xff000000u|(uint32_t(40+x/2)<<16)|(uint32_t(80+y/2)<<8)|190u;
            const auto original=image.pixels;
            manager.Create(image,image,{240,220},std::nullopt,{},false);auto& p=*manager.pins_.begin()->second;
            expect(p.style==DefaultPinStyle&&p.paper.inset<=2,"new pins default to a minimal border");
            p.dpi=96;const auto initial=MakePaperLayout(image.Width(),image.Height(),p.dpi,p.style);
            manager.ApplyPaper(p,{float(240-initial.inset-initial.shadow),float(220-initial.inset-initial.shadow)});
            auto sheet=MakeFrame({0,0,2400,480},0xffe4e8ed);
            for(int value=0;value<=4;++value){
                selected=static_cast<PinStyle>(value);manager.RefreshAppearance();
                expect(p.style==selected&&!p.ocr_enabled&&p.version==0,"style updates live without triggering OCR");
                RECT bounds{};GetWindowRect(p.window,&bounds);
                expect(bounds.left+p.Inset()==240&&bounds.top+p.Inset()==220,"style switches preserve image screen position");
                auto rendered=MakeFrame({0,0,p.CanvasWidth(),p.CanvasHeight()},0);manager.Paint(p);
                std::copy_n(p.surface->Pixels(),rendered.pixels.size(),rendered.pixels.begin());
                expect(Crop(rendered,{p.Inset(),p.Inset(),p.Inset()+image.Width(),p.Inset()+image.Height()}).pixels==original,"every style preserves all screenshot pixels including corners");
                const auto builds=p.shadow_builds;manager.RefreshAppearance();manager.Paint(p);
                expect(p.shadow_builds==builds,"unchanged style and repaint reuse shadow cache");
                expect(p.image->pixels==original&&FlattenTextMarks(*p.image,p.marks).pixels==original,"export input excludes all sticker decoration");
                if(selected==PinStyle::None)expect(p.Inset()==0&&p.shadow_image.pixels.empty(),"none has zero border and no shadow allocation");
                if(selected==PinStyle::Polaroid)expect(p.paper.height>image.Height()+2*p.paper.inset,"polaroid has an asymmetric bottom margin");
                const int x0=value*480+(480-rendered.Width())/2,y0=(480-rendered.Height())/2;
                for(int y=0;y<rendered.Height();++y)for(int x=0;x<rendered.Width();++x){
                    const auto src=rendered.pixels[static_cast<size_t>(y)*rendered.Width()+x];auto& dst=sheet.pixels[static_cast<size_t>(y+y0)*sheet.Width()+x+x0];
                    const UINT inv=255-(src>>24);uint32_t out=0xff000000;
                    for(UINT shift:{0u,8u,16u})out|=std::min(255u,((src>>shift)&255)+((((dst>>shift)&255)*inv+127)/255))<<shift;dst=out;
                }
                for(float dpi:{96.f,144.f,192.f})for(POINT size:{POINT{1,1},POINT{1,160},POINT{280,1},POINT{280,160}}){
                    const auto layout=MakePaperLayout(size.x,size.y,dpi,selected);HRGN region=PaperRegion(layout);
                    expect(PtInRegion(region,layout.inset,layout.inset)&&PtInRegion(region,layout.inset+size.x-1,layout.inset+size.y-1),"style hit region includes both image corners at mixed DPI and narrow sizes");DeleteObject(region);
                    const auto point=layout.ToImage(layout.ToWindow({-2.5f,14.25f}));expect(point.x==-2.5f&&point.y==14.25f,"style coordinate mapping roundtrips");
                    const auto shadow=PaperShadowImage(layout);
                    if(layout.shadow){
                        bool clear=true;for(int y=0;y<size.y;++y)for(int x=0;x<size.x;++x)clear&=shadow.pixels[static_cast<size_t>(layout.shadow+layout.inset+y)*shadow.Width()+layout.shadow+layout.inset+x]==0;
                        expect(clear,"standalone shadow never overlays screenshot content");
                    }
                }
                p.zoom=p.zoom_goal=1.5f;manager.ApplyPaper(p,{100.25f,120.5f});p.locked=true;
                RECT old{};GetWindowRect(p.window,&old);const auto point=p.WindowPoint({100,80});
                selected=value==4?PinStyle::None:static_cast<PinStyle>(value+1);manager.RefreshAppearance();
                RECT next{};GetWindowRect(p.window,&next);const auto after=p.WindowPoint({100,80});
                expect(p.locked&&p.zoom==1.5f&&std::abs(old.left+point.x-next.left-after.x)<.01f&&std::abs(old.top+point.y-next.top-after.y)<.01f,"restyling preserves lock, zoom and subpixel image position");
                p.locked=false;p.zoom=p.zoom_goal=1;
                const auto reset=MakePaperLayout(image.Width(),image.Height(),p.dpi,p.style);
                manager.ApplyPaper(p,{float(240-reset.inset-reset.shadow),float(220-reset.inset-reset.shadow)});
            }
            SavePng(sheet,L"pin-styles-preview.png");
            for(int value=0;value<=4;++value){selected=static_cast<PinStyle>(value);manager.Create(image,image,{240,220},std::nullopt,{},false);auto& pin=*manager.pins_.rbegin()->second;expect(pin.style==selected,"new pin uses currently selected style");}
            selected=PinStyle::None;manager.RefreshAppearance();bool all=true;for(const auto& [id,pin]:manager.pins_)all&=pin->style==PinStyle::None&&pin->shadow_image.pixels.empty();expect(all,"all existing pins update and discard old shadows");
        }
        DestroyWindow(host);return failures?1:0;
    }
};
}
int main(){SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int result=1;try{result=lumashot::PinTest::Run();}catch(const std::exception& e){std::cout<<"FAIL "<<e.what()<<'\n';}CoUninitialize();return result;}
