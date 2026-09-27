// Synthetic images only; exercise the production layered-window render path.
#include "../src/pin/pin.cpp"
#include <chrono>
#include <iostream>
#include <numeric>
namespace lumashot {
struct PinTest {
static int Run(){
 int failures=0;const auto expect=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';failures+=!ok;};
 HWND host=CreateWindowExW(0,L"STATIC",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
 {
  PinManager manager(host);PinStyle style=PinStyle::Simple;manager.sticker_style=[&]{return style;};
  for(POINT size:{POINT{1280,720},POINT{2560,1440},POINT{3840,2160}}){
   auto image=MakeFrame({0,0,size.x,size.y},0xffedf3fa);for(int y=0;y<size.y;++y)for(int x=0;x<size.x;++x)if((x/24+y/24)%2)image.pixels[size_t(y)*size.x+x]=0xff4779ae;
   manager.Create(image,image,{100,100},std::nullopt,{},false);auto& p=*manager.pins_.rbegin()->second;
   for(auto selected:{PinStyle::Simple,PinStyle::Curl}){
    style=selected;manager.RefreshAppearance();const auto builds=p.shadow_builds;
    std::vector<double> times;const POINT anchor{420,340};const Point image_anchor{320,240};
    for(int i=0;i<24;++i){p.zoom=.55f+float(i%12)*.025f;
     const auto layout=MakePaperLayout(int(std::lround(size.x*p.zoom)),int(std::lround(size.y*p.zoom)),p.dpi,p.style);
     const auto started=std::chrono::steady_clock::now();manager.ApplyPaper(p,{anchor.x-layout.inset-layout.shadow-image_anchor.x*p.zoom,anchor.y-layout.inset-layout.shadow-image_anchor.y*p.zoom});
     times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());
     RECT r{};GetWindowRect(p.window,&r);const auto q=p.WindowPoint(image_anchor);expect(std::abs(r.left+q.x-anchor.x)<.05f&&std::abs(r.top+q.y-anchor.y)<.05f,"zoom preserves cursor anchor including fractional origin");
    }
    std::sort(times.begin(),times.end());std::cout<<"PERF "<<size.x<<'x'<<size.y<<" style="<<int(selected)<<" mean_ms="<<std::accumulate(times.begin(),times.end(),0.)/times.size()<<" p95_ms="<<times[22]<<" shadow_builds="<<p.shadow_builds-builds<<'\n';
    expect(!p.ocr_enabled&&p.version==0,"plain pin zoom never starts OCR");
   }
   p.zoom_goal=p.zoom;RECT r{};GetWindowRect(p.window,&r);POINT anchor{r.left+p.Inset()+100,r.top+p.Inset()+100};
   const auto old=p.zoom_goal;p.locked=true;manager.Zoom(p,120,anchor);expect(p.zoom_goal==old,"locked pin ignores wheel");p.locked=false;
   manager.Zoom(p,60,anchor);manager.Zoom(p,60,anchor);expect(std::abs(p.zoom_goal-old*1.12f)<.0001f,"high resolution wheel deltas accumulate without loss");
   p.zoom_started=GetTickCount64()-200;SendMessageW(p.window,WM_TIMER,101,0);expect(p.zoom==p.zoom_goal,"animation reaches exact final scale");
   expect(p.image->pixels==image.pixels,"zoom never changes source pixels");
   DestroyWindow(p.window);manager.Ready();
  }
  for(auto selected:{PinStyle::None,PinStyle::Simple,PinStyle::Rounded,PinStyle::Polaroid,PinStyle::Curl})for(float dpi:{96.f,144.f,192.f})for(POINT size:{POINT{9,5},POINT{960,540}}){
   style=selected;auto image=MakeFrame({0,0,size.x,size.y},0xffcce5ff);manager.Create(image,image,{80,80},std::nullopt,{},false);auto& p=*manager.pins_.rbegin()->second;p.dpi=dpi;p.zoom=.83f;
   manager.ApplyPaper(p,{-40.65f,80.7f});const auto patch=p.shadow_image;
   const auto count=size_t(p.CanvasWidth())*p.CanvasHeight();const std::vector<uint32_t> optimized(p.surface->Pixels(),p.surface->Pixels()+count);
   p.shadow_image=PaperShadowImage(p.paper,true);p.shadow_bitmap.Reset();manager.Paint(p);int worst=0;
   for(size_t i=0;i<count;++i)for(int shift:{0,8,16,24})worst=std::max(worst,std::abs(int((optimized[i]>>shift)&255)-int((p.surface->Pixels()[i]>>shift)&255)));
   std::cout<<"PIXELS style="<<int(style)<<" dpi="<<dpi<<" size="<<size.x<<'x'<<size.y<<" max_channel_delta="<<worst<<'\n';expect(worst<=2,"fractional nine-slice composite matches original full shadow within two channel levels");
   p.shadow_image=patch;p.shadow_bitmap.Reset();manager.Paint(p);const auto builds=p.shadow_builds;manager.Paint(p);expect(p.shadow_builds==builds,"unchanged paint reuses shadow");
   if(size.x==960&&dpi==96){auto preview=MakeFrame({0,0,p.CanvasWidth(),p.CanvasHeight()},0);std::copy_n(p.surface->Pixels(),count,preview.pixels.begin());SavePng(preview,L"build/pin-zoom-style-"+std::to_wstring(int(style))+L".png");}
   DestroyWindow(p.window);manager.Ready();
  }
  style=PinStyle::Simple;auto input_image=MakeFrame({0,0,320,180},0xffcce5ff);manager.Create(input_image,input_image,{100,100},std::nullopt,{},false);auto& p=*manager.pins_.rbegin()->second;
  const auto deadline=GetTickCount64()+180;int ticks=0;while(GetTickCount64()<deadline){
   SendMessageW(p.window,WM_MOUSEWHEEL,MAKEWPARAM(0,1),MAKELPARAM(180,160));MSG message{};
   while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.hwnd==p.window&&message.message==WM_TIMER&&message.wParam==101)++ticks;TranslateMessage(&message);DispatchMessageW(&message);}Sleep(2);
  }
  std::cout<<"INPUT continuous_wheel_ticks="<<ticks<<'\n';expect(ticks>=2&&p.zoom>1,"continuous high-rate wheel input cannot starve animation timer");
  manager.ToggleLock(p);const float stopped=p.zoom;SendMessageW(p.window,WM_TIMER,101,0);expect(!p.zoom_animating&&p.zoom==stopped,"lock stops animation and ignores queued stale ticks");
  DestroyWindow(p.window);manager.Ready();
 }
 DestroyWindow(host);return failures?1:0;
}
};
}
int main(){SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int result=1;try{result=lumashot::PinTest::Run();}catch(const std::exception& e){std::cout<<"FAIL "<<e.what()<<'\n';}CoUninitialize();return result;}
