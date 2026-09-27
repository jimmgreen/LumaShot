#include "ui/render.h"
#include "model/arrow.h"
#include "model/number.h"
#include "model/note_color.h"
#include "capture/cursor.h"
#include "export/png.h"
#include <objbase.h>
#include <iostream>
#include <algorithm>
#include <wincodec.h>

using namespace lumashot;
static int failures{};
static void Expect(bool value,const char* text) {
    std::cout<<(value?"[PASS] ":"[FAIL] ")<<text<<'\n';if(!value)++failures;
}
int main() {
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 2;
    try {
        {
            bool readable=true;
            for(uint32_t background:{0xffffffffu,0xff171717u,0xff808080u})for(uint32_t fill:{0xffff574fu,0xff1686ffu,0xff243142u,0xffffffffu,0xff91df9eu,0xffffd67du,0xffcc9aebu})for(int preset=-2;preset<7;++preset){if(preset==-1)continue; // -1 is custom ink: kept as chosen, covered below
                auto frame=MakeFrame({0,0,240,100},background);Mark note;note.tool=Tool::Number;note.number_combo=NumberCombo::Text;note.a={0,0};note.b={32,32};note.number_target={230,90};note.color=fill;note.number_text_preset=preset;
                const auto appearance=ResolveNoteAppearance(frame,note);const auto actual=appearance.background?TagComposite(*appearance.background,background):background; // tint is translucent: measure what is shown
                readable=readable&&NoteContrast(NoteLuminance(appearance.ink),NoteLuminance(actual))>=4.5;
            }
            Expect(readable,"all seven note presets meet 4.5 contrast on light dark and filled backgrounds");
            auto mixed=MakeFrame({0,0,240,100},0xffffffff);for(size_t i=0;i<mixed.pixels.size();i+=2)mixed.pixels[i]=0xff000000;
            Mark note;note.tool=Tool::Number;note.number_combo=NumberCombo::Text;note.a={0,0};note.b={32,32};note.number_target={230,90};note.number_text_preset=-1;note.number_text_color=0xffaa7788;
            const auto appearance=ResolveNoteAppearance(mixed,note);
            Expect(appearance.ink==note.number_text_color&&appearance.background&&(*appearance.background>>24)<0xff,"mixed backgrounds keep custom ink and a translucent tint, never an opaque slab");
        }
        {
            ComPtr<IWICImagingFactory> wic;
            CheckWin32(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic))),"text test WIC");
            ComPtr<ID2D1Factory> shapes;D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,shapes.GetAddressOf());
            ComPtr<IDWriteFactory> fonts;DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(fonts.GetAddressOf()));
            ComPtr<IDWriteTextFormat> format;fonts->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,24,L"zh-CN",&format);
            TextRenderer text;
            for(float dpi:{96.f,144.f,192.f}) {
                ComPtr<IWICBitmap> bitmap;wic->CreateBitmap(320,180,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap);
                ComPtr<ID2D1RenderTarget> target;
                shapes->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),dpi,dpi),&target);
                target->BeginDraw();target->Clear(D2D1::ColorF(0xffffff));
                target->SetTransform(D2D1::Matrix3x2F::Translation(10,5));
                const auto stats=text.Draw(target.Get(),fonts.Get(),L"LumaShot 中文\nTest",format.Get(),{0,0,130,75},D2D1::ColorF(0));
                Expect(SUCCEEDED(target->EndDraw()),"DLL text drawing completes on recreated targets and mixed DPI");
                Expect(stats.freetype_glyphs>0,"prebuilt DLL actually rasterizes FreeType glyphs");
                std::vector<uint32_t> pixels(320*180);
                bitmap->CopyPixels(nullptr,320*4,static_cast<UINT>(pixels.size()*4),reinterpret_cast<BYTE*>(pixels.data()));
                Expect(std::any_of(pixels.begin(),pixels.end(),[](uint32_t p){return (p&0xffffff)!=0xffffff;}),"DLL produces visible text pixels");
                bool outside=true;const float scale=dpi/96;
                for(int y=0;y<180;++y)for(int x=0;x<320;++x)
                    if(x<10*scale-1||x>=140*scale+1||y<5*scale-1||y>=80*scale+1)outside=outside&&pixels[static_cast<size_t>(y)*320+x]==0xffffffff;
                Expect(outside,"text clipping respects target transform and DPI");
            }
        }
        auto frame=MakeFrame({-100,-50,220,190},0xffffffff);
        auto cursor=FrozenCursor::Copy(LoadCursorW(nullptr,IDC_HAND),{-80,-30});cursor.Composite(frame);
        Document document;
        Mark rectangle;rectangle.a={0,0};rectangle.b={80,60};rectangle.width=4;rectangle.color=0xffff0000;
        document.Add(rectangle);
        Mark arrow;arrow.tool=Tool::Arrow;arrow.a={160,100};arrow.b={80,50};document.Add(arrow);
        Expect(document.marks.size()==2&&document.CanUndo(),"adding annotations creates undo history");
        document.Undo();Expect(document.marks.size()==1&&document.CanRedo(),"undo removes the last annotation");
        document.Redo();Expect(document.marks.size()==2,"redo restores the annotation");
        document.selected=0;document.DeleteSelected();Expect(document.marks.size()==1,"selected annotation can be deleted");
        document.Undo();Expect(document.marks.size()==2,"deletion can be undone");
        Renderer renderer;auto output=renderer.Flatten(frame,document,frame.bounds);
        const size_t edge=static_cast<size_t>(50)*320+130;
        Expect((output.pixels[edge]&0xffffff)==0xff0000,"rectangle renders at physical desktop coordinates");
        bool same=true;
        for(int y=0;y<40;++y)for(int x=0;x<35;++x)if(output.pixels[static_cast<size_t>(y)*320+x]!=frame.pixels[static_cast<size_t>(y)*320+x])same=false;
        Expect(same,"annotation export preserves the frozen cursor");
        Renderer clean_renderer;auto untouched=clean_renderer.Flatten(frame,Document{},frame.bounds);
        Expect(untouched.pixels==frame.pixels,"empty annotation export is pixel exact");
        const auto path=std::filesystem::temp_directory_path()/(L"LumaShot-render-test-"+std::to_wstring(GetCurrentProcessId())+L".png");
        SavePng(output,path);auto decoded=ReadPng(path);std::filesystem::remove(path);
        Expect(decoded.pixels==output.pixels,"PNG preserves annotations and cursor together");
        auto pattern=MakeFrame({0,0,32,32});
        for(int y=0;y<32;++y)for(int x=0;x<32;++x)pattern.pixels[y*32+x]=(x+y)%2?0xffffffff:0xff000000;
        Mark mosaic;mosaic.tool=Tool::Mosaic;mosaic.a={0,0};mosaic.b={16,16};mosaic.width=2;
        Document redacted;redacted.Add(mosaic);Renderer mosaic_renderer;auto result=mosaic_renderer.Flatten(pattern,redacted,pattern.bounds);
        Expect((result.pixels[0]&0xffffff)==0x7f7f7f,"mosaic averages image cells instead of painting a cosmetic overlay");
        Expect(result.pixels[31]==pattern.pixels[31],"mosaic preserves pixels outside the redacted region");
        for (int direction=0; direction<3; ++direction) {
            Mark stroke;stroke.tool=Tool::Mosaic;stroke.width=2;
            stroke.a={8,8};stroke.b=direction==0?Point{24,8}:(direction==1?Point{8,24}:Point{8,8});
            stroke.points={stroke.a,stroke.b};
            Document painted;painted.Add(stroke);Renderer stroke_renderer;
            auto painted_image=stroke_renderer.Flatten(pattern,painted,pattern.bounds);
            Expect(painted_image.pixels!=pattern.pixels,
                direction==0?"horizontal mosaic stroke changes pixels":(direction==1?"vertical mosaic stroke changes pixels":"single mosaic dab changes pixels"));
            painted.Undo();Renderer undone_renderer;
            Expect(undone_renderer.Flatten(pattern,painted,pattern.bounds).pixels==pattern.pixels,"mosaic stroke can be undone without altering the source");
            painted.Redo();Renderer redone_renderer;
            Expect(redone_renderer.Flatten(pattern,painted,pattern.bounds).pixels==painted_image.pixels,"mosaic redo reproduces the exact result");
        }
        auto negative_pattern=MakeFrame({-500,-300,-180,-60});
        for(int y=0;y<negative_pattern.Height();++y)for(int x=0;x<negative_pattern.Width();++x)
            negative_pattern.pixels[static_cast<size_t>(y)*negative_pattern.Width()+x]=(x+y)%2?0xffffffff:0xff000000;
        Mark negative_stroke;negative_stroke.tool=Tool::Mosaic;negative_stroke.a={-440,-240};negative_stroke.b={-320,-240};
        negative_stroke.points={negative_stroke.a,negative_stroke.b};negative_stroke.mosaic_cell=20;negative_stroke.mosaic_brush=40;
        Document negative_marks;negative_marks.Add(negative_stroke);Renderer negative_renderer;
        const auto masked=negative_renderer.Flatten(negative_pattern,negative_marks,negative_pattern.bounds);
        Expect(masked.pixels[60*320+120]!=negative_pattern.pixels[60*320+120],"mosaic bridges a long horizontal gesture on a negative-origin display");
        Expect(masked.pixels.back()==negative_pattern.pixels.back(),"mosaic preserves distant pixels");
        const auto mosaic_path=std::filesystem::temp_directory_path()/(L"LumaShot-mosaic-test-"+std::to_wstring(GetCurrentProcessId())+L".png");
        SavePng(masked,mosaic_path);const auto mosaic_png=ReadPng(mosaic_path);std::filesystem::remove(mosaic_path);
        Expect(mosaic_png.pixels==masked.pixels,"saved PNG contains the actual mosaic pixels");
        for(float scale:{1.0f,1.5f,2.0f}) {
            const RECT monitor{-1280,0,0,1024};
            auto layout=PlaceToolbar({-100,900,-5,1000},monitor,scale);
            Expect(layout.tools.left>=monitor.left&&layout.tools.right<=monitor.right&&layout.colors.top>=monitor.top&&layout.tools.bottom<=monitor.bottom,
                "toolbar fits negative-origin display at mixed DPI scales");
            const Box button=layout.Button(12);
            Expect(layout.Hit({(button.left+button.right)/2,(button.top+button.bottom)/2})==12,"toolbar hit testing matches painted buttons");
            bool aligned=true;
            for(int i:{20,21,22,23,28,27,40,41,42,43,44,45,46,47}) {
                const Box property=layout.Property(i);
                aligned=aligned&&Contains(layout.colors,{property.left,property.top})&&Contains(layout.colors,{property.right,property.bottom})&&
                    layout.Hit({(property.left+property.right)/2,(property.top+property.bottom)/2})==i;
            }
            Expect(aligned,"all property controls stay inside the panel and respond at their painted positions");
        }
        {
            const auto background=MakeFrame({0,0,240,160},0xffffffff);
            const auto draw=[&](Mark mark){Document doc;doc.Add(mark);Renderer r;return r.Flatten(background,doc,background.bounds);};
            Mark mark;mark.a={30,40};mark.b={210,120};mark.width=3;
            const auto solid=draw(mark);
            for(auto style:{LineStyle::Dash,LineStyle::Dot,LineStyle::DashDot}){mark.line_style=style;Expect(draw(mark).pixels!=solid.pixels,"rectangle line style changes exported pixels");}
            mark.tool=Tool::Arrow;mark.a={30,80};mark.b={210,80};mark.line_style=LineStyle::Solid;mark.arrow_size=24;
            const auto filled=draw(mark);
            mark.line_style=LineStyle::Dash;Expect(draw(mark).pixels!=filled.pixels,"arrow shaft line style changes exported pixels");
            mark.line_style=LineStyle::Solid;
            for(auto style:{ArrowStyle::Open,ArrowStyle::Double}){mark.arrow_style=style;Expect(draw(mark).pixels!=filled.pixels,"arrow head style changes exported pixels");}
            mark.arrow_style=ArrowStyle::Filled;mark.arrow_size=48;Expect(draw(mark).pixels!=filled.pixels,"arrow head size changes exported pixels");
            Document history;history.Add(mark);history.Undo();history.Redo();Expect(history.marks.size()==1&&history.marks[0]==mark,"undo redo preserves arrow properties");
            mark.a=mark.b;draw(mark);
            {Mark label;label.tool=Tool::Number;label.a={10,20};label.b={42,52};label.number_target={210,120};label.number_combo=NumberCombo::Text;label.text=L"Label";
                const auto original_label=draw(label);label.number_text_preset=-1;label.number_text_color=0xffef3340;const auto colored_label=draw(label);Expect(original_label.pixels!=colored_label.pixels,"label text color changes exported pixels");
                label.number_text_size=24;Expect(draw(label).pixels!=colored_label.pixels,"label font size changes exported pixels independently");
                Document labels;labels.Add(label);labels.Undo();labels.Redo();Expect(labels.marks[0]==label,"label text appearance survives undo redo");}
            for(float scale:{1.f,1.5f,2.f}){const auto pin=NumberPin({-100,20,-100+48*scale,20+62.4f*scale});bool tangent=true;
                for(auto contact:{pin.left,pin.right}){const float dot=(contact.x-pin.center.x)*(pin.tip.x-contact.x)+(contact.y-pin.center.y)*(pin.tip.y-contact.y);tangent=tangent&&std::abs(dot)<.02f&&std::abs(std::hypot(contact.x-pin.center.x,contact.y-pin.center.y)-pin.radius)<.01f;}
                Expect(tangent,"pin edges are mathematically tangent to the circular head at mixed DPI");}
            for(int combo=1;combo<=6;++combo){Mark badge;badge.tool=Tool::Number;badge.a={20,40};badge.b={52,72};badge.number_size=32;badge.number_combo=static_cast<NumberCombo>(combo);badge.number_target={160,85};badge.text=L"Test";
                Document grouped;grouped.Add(badge);grouped.Checkpoint();Translate(grouped.marks[0],{10,5});Expect(grouped.marks[0].number_target==Point{170,90},"moving number combination moves the attachment");grouped.Undo();Expect(grouped.marks[0]==badge,"undo restores the entire number combination");Expect(draw(badge).pixels!=background.pixels,"number combination exports through shared renderer");}
            for(int shape=0;shape<10;++shape){Mark badge;badge.tool=Tool::Number;badge.a={80,40};badge.b={128,88};badge.number=12;badge.number_shape=static_cast<NumberShape>(shape);Expect(draw(badge).pixels!=background.pixels,"number shape renders into exported image");Document numbered;numbered.Add(badge);numbered.Undo();numbered.Redo();Expect(numbered.marks[0]==badge,"number shape and value survive undo redo");}
            for(int type=0;type<6;++type)for(int head=0;head<11;++head){
                Mark variant;variant.tool=Tool::Arrow;variant.a={210,100};variant.b={30,60};variant.arrow_type=static_cast<ArrowType>(type);variant.arrow_head=static_cast<ArrowHead>(head);variant.arrow_size=28;
                const auto geometry=BuildArrow(variant);const auto bounds=Bounds(variant);bool contained=!geometry.empty();
                for(const auto& part:geometry)for(const auto p:part.points)contained=contained&&std::isfinite(p.x)&&std::isfinite(p.y)&&Contains(bounds,p,.01f);
                Expect(contained,"variant type/head geometry remains finite and inside selection bounds");
                Expect(draw(variant).pixels!=background.pixels,"variant type/head combination exports visible pixels");
                Document arrows;arrows.Add(variant);arrows.Undo();arrows.Redo();Expect(arrows.marks[0]==variant,"variant type/head survive undo redo");
                variant.b={209.5f,100};Expect(!BuildArrow(variant).empty(),"very short variant remains drawable");
            }
            Mark hand;hand.tool=Tool::Arrow;hand.a={20,80};hand.b={200,80};hand.arrow_type=ArrowType::HandDrawn;hand.points={{20,80},{70,20},{130,140},{200,80}};
            for(auto type:{ArrowType::Standard,ArrowType::Curved,ArrowType::HandDrawn,ArrowType::Double})for(float width:{1.f,4.f,12.f}){
                Mark hollow=hand;hollow.arrow_type=type;hollow.arrow_head=ArrowHead::Hollow;hollow.arrow_size=32;hollow.width=width;
                const auto paths=BuildArrow(hollow);bool outside=true;
                for(size_t h=1;h<paths.size();++h){const auto& outline=paths[h].points;if(outline.size()!=3)continue;
                    const Point rear{(outline[1].x+outline[2].x)/2,(outline[1].y+outline[2].y)/2};
                    const float length=std::hypot(outline[0].x-rear.x,outline[0].y-rear.y);
                    const Point normal{(outline[0].x-rear.x)/length,(outline[0].y-rear.y)/length};
                    const auto end=h==1?paths.front().points.back():paths.front().points.front();
                    outside=outside&&((end.x-rear.x)*normal.x+(end.y-rear.y)*normal.y+width*.5f<=.01f);
                }
                Expect(outside,"hollow arrow shaft and round cap stop outside rear edge for all widths");
            }
            const auto hand_path=BuildArrow(hand);Expect(hand_path.front().points.size()>hand.points.size()&&hand_path.front().points.front()==hand.a,"hand drawn arrow renders a smooth sampled curve with the original start");
            mark.tool=Tool::Text;mark.a={20,20};mark.b={220,150};mark.text=L"字号 Test";mark.font_size=16;
            const auto small_text=draw(mark);mark.font_size=32;Expect(draw(mark).pixels!=small_text.pixels,"font size changes exported text pixels");
            mark.font_size=16;mark.text_bold=true;Expect(draw(mark).pixels!=small_text.pixels,"bold text changes exported glyph pixels");
            mark.text_bold=false;
            for(auto align:{TextAlign::Center,TextAlign::Right}){mark.text_align=align;Expect(draw(mark).pixels!=small_text.pixels,"text alignment changes exported position");}
            mark.text_background=TextBackground::Yellow;const auto backed=draw(mark);
            Expect(backed.pixels[140*240+210]==0xffffe58f,"text background fills the text box in exported pixels");
            history.Reset();history.Add(mark);history.Undo();history.Redo();Expect(history.marks.size()==1&&history.marks[0]==mark,"undo redo preserves text appearance properties");
            mark=Mark{};mark.tool=Tool::Pen;mark.color=0xff000000;mark.width=8;mark.points={{30,80},{70,80},{110,80},{190,80}};mark.pen_opacity=.5f;
            const auto translucent=draw(mark);
            const auto shade=translucent.pixels[80*240+70]&255;
            Expect(shade>=126&&shade<=129,"pen opacity blends the stroke at fifty percent");
            Expect(translucent.pixels[80*240+70]==translucent.pixels[80*240+50],"pen segment joints do not darken translucent strokes");
            mark.pen_opacity=0;Expect(draw(mark).pixels==background.pixels,"fully transparent pen preserves the image");
            mark.pen_opacity=1;mark.points={{30,80},{80,30},{120,120},{190,80}};mark.pen_smoothing=PenSmoothing::None;
            const auto unsmoothed=draw(mark);mark.pen_smoothing=PenSmoothing::Standard;const auto standard=draw(mark);
            Expect(standard.pixels!=unsmoothed.pixels,"standard smoothing changes pen corners");
            mark.pen_smoothing=PenSmoothing::High;Expect(draw(mark).pixels!=standard.pixels,"high smoothing changes pen corners further");
            history.Reset();history.Add(mark);history.Undo();history.Redo();Expect(history.marks.size()==1&&history.marks[0]==mark,"undo redo preserves pen appearance");
            mark.pen_mode=PenMode::Highlighter;mark.color=0xffffff00;mark.width=24;mark.pen_opacity=.4f;mark.pen_smoothing=PenSmoothing::None;mark.points={{30,80},{190,80}};
            const auto highlight=draw(mark);
            Expect(highlight.pixels[80*240+29]==background.pixels[80*240+29]&&highlight.pixels[80*240+31]!=background.pixels[80*240+31],"highlighter has flat ends");
            Expect((highlight.pixels[80*240+70]&255)>=151&&(highlight.pixels[80*240+70]&255)<=154,"highlighter exports translucent yellow");
            mark.points={{30,80},{190,80},{30,80}};const auto retraced=draw(mark);
            Expect(retraced.pixels[80*240+70]==highlight.pixels[80*240+70],"retracing within one highlighter stroke does not build opacity");
            mark.points={{80,80}};Expect(draw(mark).pixels[70*240+70]!=background.pixels[70*240+70],"single highlighter click makes a square mark");
            mark.pen_opacity=0;Expect(draw(mark).pixels==background.pixels,"transparent highlighter preserves pixels");
            history.Reset();history.Add(mark);history.Undo();history.Redo();Expect(history.marks[0]==mark,"highlighter mode survives undo redo");
            auto blur_source=MakeFrame({-100,-50,140,110},0xff000000);
            for(int y=0;y<160;++y)for(int x=120;x<240;++x)blur_source.pixels[static_cast<size_t>(y)*240+x]=0xffffffff;
            Mark blur_mark;blur_mark.tool=Tool::Mosaic;blur_mark.a={-30,-10};blur_mark.b={80,80};blur_mark.mosaic_mode=MosaicMode::Blur;blur_mark.mosaic_method=MosaicMethod::Rectangle;blur_mark.mosaic_cell=12;
            Document blurred_doc;blurred_doc.Add(blur_mark);Renderer blur_renderer;const auto blur_result=blur_renderer.Flatten(blur_source,blurred_doc,blur_source.bounds);
            const auto blurred_edge=blur_result.pixels[80*240+120]&255;
            Expect(blurred_edge>0&&blurred_edge<255,"blur mode creates intermediate colors at an edge");
            Expect(blur_result.pixels.front()==blur_source.pixels.front()&&blur_result.pixels[80*240+20]==blur_source.pixels[80*240+20],"rectangle blur preserves pixels outside its region at negative origins");
            blur_mark.mosaic_cell=36;blurred_doc.Reset();blurred_doc.Add(blur_mark);Expect(blur_renderer.Flatten(blur_source,blurred_doc,blur_source.bounds).pixels!=blur_result.pixels,"blur strength changes the actual filter extent");
            blur_mark.mosaic_method=MosaicMethod::Brush;blur_mark.a=blur_mark.b={20,30};blur_mark.points={blur_mark.a};blur_mark.mosaic_brush=24;blurred_doc.Reset();blurred_doc.Add(blur_mark);
            Expect(blur_renderer.Flatten(blur_source,blurred_doc,blur_source.bounds).pixels!=blur_source.pixels,"single blur brush dab modifies the captured image");
        }
        const auto narrow=PlaceToolbar({0,0,200,200},{0,0,480,800},2);
        Expect(narrow.columns<13&&narrow.tools.right<=480,"toolbar wraps on narrow high-DPI displays");
        bool property_wrap=true;
        for(int i:{20,21,22,23,28,27,40,41,42,43,44,45,46,47}){const Box p=narrow.Property(i);property_wrap=property_wrap&&Contains(narrow.colors,{p.left,p.top})&&Contains(narrow.colors,{p.right,p.bottom});}
        Expect(property_wrap,"properties wrap without clipping on a narrow display");
    }catch(const std::exception& e){std::cout<<"[FAIL] "<<e.what()<<'\n';++failures;}
    CoUninitialize();std::cout<<"Failures: "<<failures<<'\n';return failures?1:0;
}
