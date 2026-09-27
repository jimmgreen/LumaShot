#include "ui/render.h"
#include "export/png.h"
#include "mosaic_reference.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <wincodec.h>
#include <filesystem>
#include <stdexcept>
using namespace lumashot;
namespace {
int failures{},checks{};
void Expect(bool value,const char* text){++checks;if(!value){++failures;std::cout<<"FAIL "<<text<<'\n';}}
void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Synthetic graphics operation failed");}
bool Equal(const Frame& a,const Frame& b){return EqualRect(&a.bounds,&b.bounds)&&a.pixels==b.pixels;}
Frame Pattern(RECT bounds){auto f=MakeFrame(bounds);for(size_t i=0;i<f.pixels.size();++i)f.pixels[i]=0xff000000|static_cast<uint32_t>((i*2654435761u)&0xffffff);return f;}
template<class F> double Measure(F action,int count=8){action();std::vector<double> samples;for(int i=0;i<count;++i){const auto start=std::chrono::steady_clock::now();action();samples.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());}std::sort(samples.begin(),samples.end());return samples[samples.size()/2];}
volatile size_t consumed{};
void Snapshot(){
    Document doc;Mark mark;mark.tool=Tool::Pen;mark.points={{1,2},{3,4}};doc.Add(mark);mark.color=0xff123456;doc.Add(mark);doc.Undo();
    doc.selected=0;doc.selected_part=EditPart::Detail;
    auto copy=doc.Snapshot();Expect(copy.marks==doc.marks&&copy.selected==doc.selected&&copy.selected_part==doc.selected_part,"snapshot preserves current editable state");
    Expect(!copy.CanUndo()&&!copy.CanRedo()&&doc.CanUndo()&&doc.CanRedo(),"snapshot excludes histories without changing original histories");
    copy.marks[0].points[0].x=99;Expect(doc.marks[0].points[0].x==1,"snapshot owns immutable export input independently");
    doc.Redo();Expect(doc.marks.size()==2,"original redo survives snapshot");doc.Undo();Expect(doc.marks.size()==1,"original undo survives snapshot");
}
void MosaicPixels(){
    for(const RECT bounds:{RECT{0,0,67,49},RECT{-53,-37,14,12}})for(int shape=0;shape<8;++shape)for(float radius:{2.f,3.5f,12.f,64.f,128.f}){
        const auto frame=Pattern(bounds);Mark m;m.tool=Tool::Mosaic;m.mosaic_mode=MosaicMode::Blur;m.mosaic_cell=radius;m.mosaic_brush=shape%2?7.f:33.f;
        m.a={float(bounds.left)-3.25f,float(bounds.top)+1.5f};m.b={float(bounds.right)-5.5f,float(bounds.bottom)+2.75f};
        if(shape==1)std::swap(m.a,m.b);
        if(shape==2)m.b={m.a.x+.75f,m.a.y+4};
        if(shape==3){m.a={float(bounds.right)+10,float(bounds.bottom)+10};m.b={m.a.x+12,m.a.y+15};}
        if(shape>=4){m.mosaic_method=MosaicMethod::Brush;m.points={{float(bounds.left)+4,float(bounds.top)+6}};if(shape>=5)m.points.push_back({float(bounds.right)-3,float(bounds.bottom)-5});if(shape>=6)m.points.push_back({float(bounds.left)-8,float(bounds.bottom)+3});if(shape==7)m.points.push_back(m.points.front());}
        Expect(Equal(BuildMosaicBlur(frame,m),test_reference::Blur(frame,m)),"blur is pixel exact against pre-optimization implementation");
        const auto actual=BuildMosaicTiles(frame,m),expected=test_reference::Tiles(frame,m);bool same=actual.size()==expected.size();
        for(size_t i=0;same&&i<actual.size();++i)same=EqualRect(&actual[i].bounds,&expected[i].bounds)&&actual[i].color==expected[i].color;
        Expect(same,"pixel mosaic tile coverage, order and means are unchanged");
    }
}
void Bench(){
    Document doc;Mark pen;pen.tool=Tool::Pen;pen.points.resize(5000);for(size_t i=0;i<pen.points.size();++i)pen.points[i]={float(i%800),float(i%600)};
    for(int i=0;i<24;++i)doc.marks.push_back(pen);for(int i=0;i<100;++i){doc.Checkpoint();doc.marks[0].width+=.1f;}
    const auto full=Measure([&]{Document copy=doc;consumed=reinterpret_cast<size_t>(copy.marks[0].points.data());});
    const auto snapshot=Measure([&]{auto copy=doc.Snapshot();consumed=reinterpret_cast<size_t>(copy.marks[0].points.data());});
    std::cout<<"100-history copy median_ms="<<full<<" export_snapshot_median_ms="<<snapshot<<'\n';
    for(const SIZE size:{SIZE{640,360},SIZE{1280,720},SIZE{1920,1080}}){
        auto frame=MakeFrame({0,0,size.cx,size.cy});for(int y=0;y<size.cy;++y)for(int x=0;x<size.cx;++x)frame.pixels[size_t(y)*size.cx+x]=((x/8+y/8)%2)?0xff243142:0xffedf2f7;
        Mark m;m.tool=Tool::Mosaic;m.mosaic_mode=MosaicMode::Blur;m.mosaic_method=MosaicMethod::Rectangle;m.a={0,0};m.b={float(size.cx),float(size.cy)};m.mosaic_cell=64;
        const auto before=Measure([&]{auto f=test_reference::Blur(frame,m);consumed=f.pixels.size()+f.pixels[0];},5);
        const auto after=Measure([&]{auto f=BuildMosaicBlur(frame,m);consumed=f.pixels.size()+f.pixels[0];},5);
        Expect(Equal(BuildMosaicBlur(frame,m),test_reference::Blur(frame,m)),"large blur benchmark retains exact pixels");
        std::cout<<size.cx<<'x'<<size.cy<<" blur_before_ms="<<before<<" blur_after_ms="<<after<<'\n';
    }
}
}
namespace lumashot {
struct RenderOptimizationTest {
    static Frame Draw(const Frame& frame,const Document& document,int editing){
        Renderer renderer;ComPtr<IWICImagingFactory> wic;Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)));
        ComPtr<IWICBitmap> bitmap;Check(wic->CreateBitmap(frame.Width(),frame.Height(),GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap));
        Check(renderer.factory_->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&renderer.target_));
        renderer.screen_bitmap_=renderer.Bitmap(frame);renderer.acrylic_bitmap_=renderer.Bitmap(frame);
        ViewState state;state.selected=true;state.selection={float(frame.bounds.left)+4,float(frame.bounds.top)+4,float(frame.bounds.right)-4,float(frame.bounds.bottom)-4};state.toolbar=PlaceToolbar(state.selection,frame.bounds,1);state.tool=Tool::Select;
        renderer.target_->BeginDraw();renderer.target_->SetTransform(D2D1::Matrix3x2F::Translation(-float(frame.bounds.left),-float(frame.bounds.top)));
        renderer.Chrome(frame,frame,document,std::nullopt,state,frame.bounds,editing);Check(renderer.target_->EndDraw());
        auto out=MakeFrame(frame.bounds);Check(bitmap->CopyPixels(nullptr,out.Width()*4,static_cast<UINT>(out.pixels.size()*4),reinterpret_cast<BYTE*>(out.pixels.data())));return out;
    }
    static void Editing(){
        auto frame=MakeFrame({-64,-32,836,668},0xffeef4fa);Document doc;
        for(int y=65;y<245;++y)for(int x=445;x<620;++x)frame.pixels[size_t(y)*frame.Width()+x]=((x/5+y/5)%2)?0xff243142:0xffeef4fa;
        Mark text;text.tool=Tool::Text;text.a={20,25};text.b={240,105};text.text=L"文字编辑 preview";text.font_size=26;text.text_background=TextBackground::TagLight;text.rotation=12;doc.Add(text);
        Mark number;number.tool=Tool::Number;number.a={35,145};number.b={67,177};number.number_combo=NumberCombo::Text;number.number_target={370,225};number.text=L"编号说明 details";number.number_label=L"A";number.rotation=-7;doc.Add(number);
        Mark mosaic;mosaic.tool=Tool::Mosaic;mosaic.mosaic_mode=MosaicMode::Blur;mosaic.a={405,60};mosaic.b={510,175};doc.Add(mosaic);
        for(int editing:{0,1}){
            doc.selected=editing;Document legacy=doc;
            if(legacy.marks[editing].tool==Tool::Number)legacy.marks[editing].text=L"\u200b";else legacy.marks.erase(legacy.marks.begin()+editing);legacy.selected=-1;
            const auto expected=Draw(frame,legacy,-1),actual=Draw(frame,doc,editing);
            Expect(Equal(actual,expected),"borrowed editing render matches old copied-document pixels, chrome and selection");
            Expect(doc.CanUndo()&&doc.selected==editing&&doc.marks.size()==3,"editing render does not mutate document or history");
            std::filesystem::create_directories("optimization-batch1/visuals");
            SavePng(actual,std::filesystem::path("optimization-batch1/visuals")/("editing-"+std::to_string(editing)+".png"));
        }
    }
    static void Cache(){
        const auto frame=Pattern({0,0,160,120});Document doc;Mark m;m.tool=Tool::Mosaic;m.mosaic_mode=MosaicMode::Blur;m.a={15,12};m.b={120,95};doc.marks.push_back(m);
        Renderer renderer;const auto first=renderer.Flatten(frame,doc,frame.bounds);
        auto bitmap=renderer.mosaic_cache_[0].bitmap;const auto cpu=renderer.mosaic_cache_[0].blur.pixels.data();Expect(bool(bitmap),"first blur draw caches target bitmap");
        renderer.target_->BeginDraw();renderer.Marks(frame,doc,std::nullopt);Check(renderer.target_->EndDraw());
        Expect(renderer.mosaic_cache_[0].bitmap.Get()==bitmap.Get()&&renderer.mosaic_cache_[0].blur.pixels.data()==cpu,"unchanged draw reuses CPU pixels and exact GPU bitmap identity");
        doc.marks[0].mosaic_cell=36;renderer.target_->BeginDraw();renderer.Marks(frame,doc,std::nullopt);Check(renderer.target_->EndDraw());
        Expect(renderer.mosaic_cache_[0].bitmap.Get()!=bitmap.Get(),"strength change invalidates bitmap");
        bitmap=renderer.mosaic_cache_[0].bitmap;
        const auto second=renderer.Flatten(frame,doc,frame.bounds);Renderer fresh;
        Expect(Equal(second,fresh.Flatten(frame,doc,frame.bounds))&&renderer.mosaic_cache_[0].bitmap.Get()!=bitmap.Get(),"new WIC target recreates bitmap with identical exported pixels");
        Expect(!Equal(first,second),"strength change affects exported pixels");
        renderer.ResetTarget();Expect(!renderer.mosaic_cache_[0].bitmap&&!renderer.target_,"device reset discards target-bound resources");
        HWND window=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Synthetic hidden target reset test",WS_POPUP,0,0,160,120,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!window)throw std::runtime_error("Create hidden test surface");
        try{
            ViewState state;state.dragging=true;state.selection={0,0,160,120};
            Expect(renderer.Paint(window,frame.bounds,frame,frame,doc,std::nullopt,state),"paint after reset recreates surface and blur bitmap");
            bitmap=renderer.mosaic_cache_[0].bitmap;renderer.ResetTarget();
            Expect(renderer.Paint(window,frame.bounds,frame,frame,doc,std::nullopt,state)&&renderer.mosaic_cache_[0].bitmap.Get()!=bitmap.Get(),"second device reset never reuses old target bitmap");
        }catch(...){DestroyWindow(window);throw;}
        DestroyWindow(window);
    }
};
}
int main(int argc,char** argv){
    Check(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED));
    try{Snapshot();MosaicPixels();RenderOptimizationTest::Editing();RenderOptimizationTest::Cache();if(argc>1&&std::string(argv[1])=="--bench")Bench();}
    catch(const std::exception& e){++failures;std::cout<<"FAIL exception: "<<e.what()<<'\n';}
    CoUninitialize();std::cout<<"checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
