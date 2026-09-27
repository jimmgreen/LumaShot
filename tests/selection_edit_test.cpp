#include "model/selection.h"
#include "model/number.h"
#include "pin/annotation.h"
#include "ui/selection_render.h"
#include "ui/text_renderer.h"
#include "export/png.h"
#include <wincodec.h>
#include <iostream>
#include <cmath>
using namespace lumashot;
int main(){CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int failures=0;auto expect=[&](bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<'\n';failures+=!ok;};auto close_points=[](Point a,Point b){return std::hypot(a.x-b.x,a.y-b.y)<.02f;};
    Mark rect;rect.tool=Tool::Rectangle;rect.a={100,100};rect.b={300,200};rect.corner_radius=20;rect.width=2;rect.fill_color=0xffbaddff;
    expect(EditHandles(rect,EditPart::Whole,1).size()==13,"rectangle has eight resize, one rotation and four radius handles");
    auto resized=EditMark(rect,EditPart::Whole,4,rect.b,{350,230},false,false);expect(resized.a==rect.a&&resized.b==Point{350,230},"bottom right resize keeps opposite corner");
    auto rounded=EditMark(rect,EditPart::Whole,9,{120,120},{130,130},false,false);expect(rounded.corner_radii&&(*rounded.corner_radii)[0]==30&&(*rounded.corner_radii)[1]==20,"corner radius changes independently");
    auto proportional=EditMark(rect,EditPart::Whole,4,rect.b,{400,240},true,false);expect(std::abs((proportional.b.x-proportional.a.x)/(proportional.b.y-proportional.a.y)-2)<.001f,"Shift resize preserves aspect ratio");
    rect.rotation=38;auto handles=EditHandles(rect,EditPart::Whole,1);auto changed=EditMark(rect,EditPart::Whole,4,handles[4].point,{handles[4].point.x+30,handles[4].point.y+40},false,false);expect(close_points(EditHandles(changed,EditPart::Whole,1)[0].point,handles[0].point),"rotated resize keeps opposite world corner fixed");
    expect(HitMark(rect,RotatePoint({100,150},MarkCenter(rect),38)),"hit testing follows rotated geometry");
    auto rotated=EditMark(rect,EditPart::Whole,8,handles[8].point,{300,200},false,true);expect(std::fmod(std::abs(rotated.rotation),15)<.001f,"Shift rotation snaps to 15 degrees");
    Mark text=rect;text.tool=Tool::Text;text.font_size=20;text.rotation=0;text.text_wrap_width=300;auto reflow=EditMark(text,EditPart::Whole,3,{300,150},{400,150},false,false);auto scaled=EditMark(text,EditPart::Whole,4,text.b,{500,300},true,false);expect(reflow.font_size==30&&reflow.b.y-reflow.a.y==150&&reflow.text_wrap_width==450&&scaled.font_size>20,"text side handles scale font and box together without Shift");
    for(float angle:{0.f,31.f})for(int handle=0;handle<8;++handle)for(float ratio:{.65f,1.7f}){
        auto original=text;original.rotation=angle;const auto controls=EditHandles(original,EditPart::Whole,1);const auto start=controls[handle].point,opposite=controls[(handle+4)%8].point;
        const Point destination{opposite.x+(start.x-opposite.x)*ratio,opposite.y+(start.y-opposite.y)*ratio};
        const auto result=EditMark(original,EditPart::Whole,handle,start,destination,false,false);
        expect(result.text_auto_size&&std::abs(result.font_size-original.font_size*ratio)<.01f&&close_points(EditHandles(result,EditPart::Whole,1)[(handle+4)%8].point,opposite)&&std::abs((result.b.x-result.a.x)/(result.b.y-result.a.y)-2)<.001f,"all eight text handles scale glyphs, retain automatic sizing and fix the opposite anchor after rotation");
    }
    const auto horizontal_shrink=EditMark(text,EditPart::Whole,4,text.b,{text.b.x-60,text.b.y},false,false);expect(horizontal_shrink.font_size<text.font_size,"corner drag can shrink text when only the horizontal mouse coordinate changes");
    const auto moved_text=EditMark(text,EditPart::Whole,-1,text.a,{text.a.x+20,text.a.y+30},false,false);expect(moved_text.font_size==text.font_size,"moving text does not change font size");
    Mark arrow;arrow.tool=Tool::Arrow;arrow.a={30,30};arrow.b={90,70};auto endpoint=EditMark(arrow,EditPart::Whole,25,arrow.b,{100,100},false,false);expect(endpoint.a==arrow.a&&endpoint.b==Point{100,100},"arrow endpoint edits independently");
    Mark fitted;fitted.tool=Tool::Text;fitted.a={100,100};fitted.b={550,210};fitted.font_size=36;fitted.text=L"地方没错";fitted.text_auto_size=true;fitted.text_wrap_width=450;fitted.rotation=28;
    const auto anchor=RotatePoint(fitted.a,MarkCenter(fitted),fitted.rotation);FitTextBounds(fitted);
    expect(close_points(RotatePoint(fitted.a,MarkCenter(fitted),fitted.rotation),anchor)&&fitted.b.x-fitted.a.x<160,"fitting rotated text keeps the visible starting corner fixed");
    fitted.rotation=0;fitted.text=L"地方没错\r\n第二行";FitTextBounds(fitted);expect(fitted.b.x-fitted.a.x<160&&fitted.b.y-fitted.a.y>72,"multiline text fits the longest line and both line heights");
    fitted.text=L"Bold 中文 123";fitted.text_bold=true;FitTextBounds(fitted);expect(fitted.b.x-fitted.a.x<450&&fitted.b.y-fitted.a.y<60,"mixed bold text is measured with the annotation font and fallback");
    Mark number;number.tool=Tool::Number;number.a={70,300};number.b={106,336};number.number_size=36;number.number_target={360,420};number.number_combo=NumberCombo::DashedBox;
    const auto detail=NumberDetailBounds(number);auto badge=EditMark(number,EditPart::Badge,-1,{88,318},{128,338},false,false);expect(NumberDetailBounds(badge)==Normalize(Point{128,338},number.number_target)&&badge.number_target==number.number_target&&badge.a==Point{110,320},"badge reconnects detail corner while keeping the far target fixed");
    auto detailmove=EditMark(number,EditPart::Detail,-1,{200,360},{230,390},false,false);auto expectedDetail=number;Translate(expectedDetail,{30,30});expect(detailmove==expectedDetail&&NumberDetailBounds(detailmove).left==detail.left+30,"dashed detail box moves together with its badge and target");
    number.number_combo=NumberCombo::Leader;auto points=NumberLeaderPoints(number);auto leader=EditMark(number,EditPart::Leader,21,points[1],{190,340},false,false);expect(leader.a==number.a&&NumberLeaderPoints(leader)[1]==Point{190,340},"leader elbow edits independently");
    // The penultimate vertex in target-to-badge order is storage index 1.
    for(float rotation:{0.f,30.f,-45.f})for(float mirror:{-1.f,1.f})for(float origin:{0.f,-700.f}){
        Mark fixture;fixture.tool=Tool::Number;fixture.number_combo=NumberCombo::Leader;fixture.number_size=40;fixture.a={origin+100,100};fixture.b={origin+140,140};fixture.rotation=rotation;
        const float cx=origin+120;fixture.number_leader=std::array<Point,4>{Point{cx+mirror*26,120},Point{cx+mirror*90,120},Point{cx+mirror*150,220},Point{cx+mirror*240,220}};fixture.number_target=fixture.number_leader->back();
        const auto world=[&](Point p){return RotatePoint(p,MarkCenter(fixture),rotation);};
        for(int handle:{22,23}){const auto grip=(*fixture.number_leader)[handle-20];const auto changedLeader=EditMark(fixture,EditPart::Leader,handle,world(grip),world({cx-mirror*300,grip.y+30}),false,false);
            const auto changedWorld=RotatePoint(NumberLeaderPoints(changedLeader)[0],MarkCenter(changedLeader),rotation);
            expect(close_points(changedWorld,world((*fixture.number_leader)[0])),"remote elbow and target crossing badge do not switch attachment sides");
            expect(close_points(RotatePoint(NumberLeaderPoints(changedLeader)[1],MarkCenter(changedLeader),rotation),world((*fixture.number_leader)[1])),"remote edits preserve penultimate adjacent elbow");
            Document history;history.marks={fixture};history.Checkpoint();history.marks[0]=changedLeader;history.Undo();expect(history.marks[0]==fixture,"leader side edit undoes atomically");history.Redo();expect(history.marks[0]==changedLeader,"leader side edit redoes atomically");
        }
        const auto adjacent_grip=(*fixture.number_leader)[1];const auto crossed=EditMark(fixture,EditPart::Leader,21,world(adjacent_grip),world({cx-mirror*90,adjacent_grip.y+15}),false,false);
        const auto bounds=Normalize(crossed.a,crossed.b);const auto crossedLine=NumberLeaderPoints(crossed);expect((crossedLine[0].x-(bounds.left+bounds.right)/2)*mirror<0,"only adjacent penultimate vertex crossing flips the badge side");
        const auto centered=EditMark(fixture,EditPart::Leader,21,world(adjacent_grip),world({cx,adjacent_grip.y+10}),false,false);expect(close_points(RotatePoint(NumberLeaderPoints(centered)[0],MarkCenter(centered),rotation),world((*fixture.number_leader)[0])),"adjacent vertex on center line retains previous side");
    }
    Document doc;doc.Add(rect);doc.selected=0;doc.Checkpoint();doc.marks[0]=changed;doc.Undo();expect(doc.marks[0]==rect,"one edit is one undo step");doc.Redo();expect(doc.marks[0]==changed,"redo restores complete transformed geometry");
    auto source=MakeFrame({0,0,800,500},0xffffffff);Document transform;transform.marks={rounded,leader};auto display=PinAnnotationDisplayDocument(transform,source,{-200,100,200,350});auto restored=PinAnnotationDocument(display,{-200,100,200,350},source);bool roundtrip=restored.marks.size()==transform.marks.size();for(size_t i=0;i<restored.marks.size();++i){const auto& a=restored.marks[i];const auto& b=transform.marks[i];roundtrip&=close_points(a.a,b.a)&&close_points(a.b,b.b)&&a.corner_radii==b.corner_radii&&std::abs(a.rotation-b.rotation)<.001f;if(a.number_leader)for(int j=0;j<4;++j)roundtrip&=close_points((*a.number_leader)[j],(*b.number_leader)[j]);}expect(roundtrip,"radii and component geometry survive scaled pin edit roundtrip within 0.02px");
    try{Document gallery;rect.a={70,95};rect.b={290,225};rect.rotation=0;rect.corner_radii=std::array<float,4>{24,12,34,6};gallery.marks.push_back(rect);text.a={405,110};text.b={650,205};text.text=L"可再次编辑的文字";text.rotation=-12;text.color=0xff243142;text.font_size=24;gallery.marks.push_back(text);number.a={70,335};number.b={106,371};number.number_target={420,430};gallery.marks.push_back(number);
        Renderer renderer;auto frame=renderer.Flatten(source,gallery,source.bounds);SavePng(frame,L"selection-shapes-preview.png");ComPtr<IWICImagingFactory> wic;CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic));ComPtr<IWICBitmap> bitmap;wic->CreateBitmapFromMemory(frame.Width(),frame.Height(),GUID_WICPixelFormat32bppPBGRA,frame.Width()*4,UINT(frame.pixels.size()*4),reinterpret_cast<BYTE*>(frame.pixels.data()),&bitmap);ComPtr<ID2D1Factory> factory;D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf());ComPtr<ID2D1RenderTarget> target;factory->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&target);
        target->BeginDraw();DrawSelectionEditor(target.Get(),gallery.marks[0],EditPart::Whole,1);DrawSelectionEditor(target.Get(),gallery.marks[1],EditPart::Whole,1);DrawSelectionEditor(target.Get(),gallery.marks[2],EditPart::Leader,1);CheckWin32(SUCCEEDED(target->EndDraw()),"Selection preview");bitmap->CopyPixels(nullptr,frame.Width()*4,UINT(frame.pixels.size()*4),reinterpret_cast<BYTE*>(frame.pixels.data()));SavePng(frame,L"selection-editor-preview.png");
    }catch(const std::exception& e){std::cout<<e.what()<<'\n';++failures;}
    CoUninitialize();return failures?1:0;
}

