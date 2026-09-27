#include "model/arrow.h"
#include "model/selection.h"
#include "ui/render.h"
#include "export/png.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <iostream>
#include <string_view>
using namespace lumashot;
namespace {
int failures{};
void Expect(bool ok,const char* message){std::cout<<(ok?"[PASS] ":"[FAIL] ")<<message<<'\n';if(!ok)++failures;}
float Distance(Point a,Point b){return std::hypot(a.x-b.x,a.y-b.y);}
float Turning(const std::vector<Point>& points){float sum=0;for(size_t i=2;i<points.size();++i){const auto a=points[i-2],b=points[i-1],c=points[i];if(Distance(a,b)<.001f||Distance(b,c)<.001f)continue;sum+=std::abs(std::atan2((b.x-a.x)*(c.y-b.y)-(b.y-a.y)*(c.x-b.x),(b.x-a.x)*(c.x-b.x)+(b.y-a.y)*(c.y-b.y)));}return sum;}
Mark Hand(std::vector<Point> points){Mark mark;mark.tool=Tool::Arrow;mark.arrow_type=ArrowType::HandDrawn;mark.points=std::move(points);mark.a=mark.points.front();mark.b=mark.points.back();mark.width=4;mark.arrow_size=22;mark.color=0xffe72e35;return mark;}
Mark TightHook(){auto mark=Hand({{240,160},{210,159},{175,156},{140,151},{109,142},{90,130},{88,114},{94,94},{107,76},{125,64},{145,57}});mark.width=3;mark.arrow_size=24;mark.arrow_style=ArrowStyle::Open;mark.color=0xffe84da3;return mark;}
void HeadConnectionRegression(){
    for(const float scale:{.5f,1.f,2.f})for(const float rotation:{0.f,1.5707963f,3.1415926f})for(const auto head:{ArrowHead::Filled,ArrowHead::Arc}){
        auto mark=TightHook();mark.arrow_head=head;mark.width*=scale;mark.arrow_size*=scale;
        for(auto& p:mark.points){const auto old=p;p={300+scale*(old.x*std::cos(rotation)-old.y*std::sin(rotation)),300+scale*(old.x*std::sin(rotation)+old.y*std::cos(rotation))};}mark.a=mark.points.front();mark.b=mark.points.back();
        const auto raw=mark.points;const auto paths=BuildArrow(mark);const auto& shaft=paths.front().points;const auto& wings=paths.back().points;
        const Point base{(wings.front().x+wings.back().x)/2,(wings.front().y+wings.back().y)/2};const float length=Distance(base,mark.b);const Point axis{(mark.b.x-base.x)/length,(mark.b.y-base.y)/length};
        float lateral=0;int checked=0;for(size_t segment=1;segment<shaft.size();++segment)for(int sample=0;sample<=8;++sample){const float t=float(sample)/8;const Point p{shaft[segment-1].x+(shaft[segment].x-shaft[segment-1].x)*t,shaft[segment-1].y+(shaft[segment].y-shaft[segment-1].y)*t};const float depth=(mark.b.x-p.x)*axis.x+(mark.b.y-p.y)*axis.y;if(Distance(p,mark.b)<=length*.65f&&depth>=0&&depth<=length*.65f){++checked;lateral=std::max(lateral,std::abs((p.x-mark.b.x)*axis.y-(p.y-mark.b.y)*axis.x));}}
        std::cout<<"Hook head-region lateral error: "<<lateral<<" px at scale "<<scale<<'\n';
        Expect(checked>2&&lateral<.05f*scale,"tight open/arc hook enters the head on its center axis");
        Expect(shaft.back()==mark.b&&mark.points==raw,"connection adjustment preserves tip and raw pointer samples");
    }
}
void HeadConnectionEdges(){
    for(auto points:{std::vector<Point>{{10,10},{11,9},{12,9}},std::vector<Point>{{30,30},{30,30},{38,22},{47,20},{47,20}},std::vector<Point>{{100,100},{140,80},{170,100},{170,140},{130,160},{95,140},{100,100}}}){
        auto mark=Hand(points);mark.arrow_head=ArrowHead::Arc;mark.arrow_style=ArrowStyle::Double;
        const auto paths=BuildArrow(mark);bool finite=true;for(const auto& path:paths)for(auto p:path.points)finite=finite&&std::isfinite(p.x)&&std::isfinite(p.y);
        Expect(paths.size()==3&&finite&&paths.front().points.front()==mark.a&&paths.front().points.back()==mark.b,"short repeated and closed double-arc paths preserve endpoints and finite geometry");
    }
    auto arrow=TightHook(),plain=arrow;plain.arrow_head=ArrowHead::None;const auto original=BuildArrow(plain).front().points;const auto aligned=BuildArrow(arrow).front().points;
    bool prefix=original.size()==aligned.size();for(size_t i=0;i<original.size()/2&&prefix;++i)prefix=original[i]==aligned[i];
    Expect(prefix,"head alignment preserves body sampling and the distant curve");
    auto jitter=Hand({{40,100},{75,100},{115,100},{150,100},{180,100},{205,100},{225,100},{223,102}});jitter.arrow_style=ArrowStyle::Open;
    const auto head=BuildArrow(jitter).back().points;const Point base{(head.front().x+head.back().x)/2,(head.front().y+head.back().y)/2};Expect(jitter.b.x-base.x>15&&std::abs(jitter.b.y-base.y)<8,"open head footprint direction rejects small backward release jitter");
}
void Label(Document& doc,Point p,const wchar_t* text);
void ConnectionPreview(){auto frame=MakeFrame({0,0,1000,470},0xffffffff);Document doc;
    Label(doc,{24,14},L"Open head / tight terminal bend");Label(doc,{510,14},L"Arc head / tight terminal bend");
    for(int column=0;column<2;++column){auto mark=TightHook();if(column)mark.arrow_head=ArrowHead::Arc;for(auto& p:mark.points){p.x=28+column*490+(p.x-75)*2.2f;p.y=65+(p.y-40)*2.2f;}mark.a=mark.points.front();mark.b=mark.points.back();mark.arrow_size*=2.2f;mark.width*=2.2f;doc.Add(mark);}
    Label(doc,{24,390},L"Synthetic path / native renderer");Renderer renderer;SavePng(renderer.Flatten(frame,doc,frame.bounds),L"hand-arrow-connection-preview.png");
}
Mark Noisy(){std::vector<Point> points;for(int i=0;i<=180;++i){const float t=float(i)/180;const float x=40+300*t*t;points.push_back({x,120+35*std::sin((x-40)/300*3.14159265f)+(i==0||i==180?0.f:(i%2?2.f:-2.f))});}return Hand(std::move(points));}
Mark BroadBump(){std::vector<Point> points;for(int i=0;i<=160;++i){const float t=float(i)/160,angle=3.14159265f*(1-t);float radius=300;
    if(t>.25f&&t<.7f){const float u=(t-.25f)/.45f;radius+=20*std::sin(u*6.2831853f*2)*std::sin(u*3.14159265f);}
    points.push_back({360+radius*std::cos(angle),100+radius*std::sin(angle)});}return Hand(std::move(points));}
int Reversals(const std::vector<Point>& points){int previous=0,count=0;for(size_t i=2;i<points.size();++i){const auto a=points[i-2],b=points[i-1],c=points[i];const float lengths=Distance(a,b)*Distance(b,c);if(lengths<.001f)continue;
    const float turn=((b.x-a.x)*(c.y-b.y)-(b.y-a.y)*(c.x-b.x))/lengths;if(std::abs(turn)<.002f)continue;const int sign=turn>0?1:-1;if(previous&&sign!=previous)++count;previous=sign;}return count;}
void Label(Document& doc,Point p,const wchar_t* text){Mark label;label.tool=Tool::Text;label.a=p;label.b={p.x+370,p.y+32};label.text=text;label.font_size=19;label.color=0xff323a45;doc.Add(label);}
void Preview(){auto frame=MakeFrame({0,0,1200,790},0xffffffff);Document doc;
    Label(doc,{30,18},L"Raw mouse trajectory");Label(doc,{430,18},L"Smoothed / solid head");Label(doc,{830,18},L"Smoothed / hollow head");
    const auto loop=Hand({{200,490},{198,458},{202,430},{209,397},{221,365},{224,338},{220,310},{205,282},{180,260},{150,251},{126,258},{113,277},{119,296},{140,305},{164,301},{192,282},{218,253},{242,218},{263,184},{284,158},{306,150},{327,158},{340,179},{347,208},{346,239},{338,269}});
    const auto short_line=Hand({{80,585},{88,580},{98,574},{107,567},{113,563}});
    const auto turn=Hand({{210,682},{231,680},{253,681},{278,679},{297,674},{305,659},{308,636},{311,613},{314,594},{313,596}});
    for(int column=0;column<3;++column)for(auto mark:{loop,short_line,turn}){Translate(mark,{float(column*400),0});if(column==0){mark.tool=Tool::Pen;mark.pen_smoothing=PenSmoothing::None;}else if(column==2)mark.arrow_head=ArrowHead::Hollow;doc.Add(mark);}
    Label(doc,{30,727},L"Top: loop + S turn");Label(doc,{430,727},L"Bottom: short stroke + sharp turn");
    Renderer renderer;SavePng(renderer.Flatten(frame,doc,frame.bounds),L"docs/hand-arrow-preview.png");
}
void FitPreview(){auto frame=MakeFrame({0,0,1440,1240},0xffffffff);Document doc;Label(doc,{30,18},L"Raw mouse trajectory");Label(doc,{750,18},L"Overall fitted arrow");
    std::vector<Mark> examples{BroadBump(),Hand({{75,690},{91,653},{109,625},{134,597},{156,565},{166,548},{180,552},{184,540},{174,528},{194,503},{226,482},{259,480},{295,500},{322,529},{336,549},{349,552},{350,542},{359,560},{374,584},{400,603},{428,602},{453,583},{480,553},{505,528},{535,507},{568,503},{600,516},{622,542}}),
        Hand({{80,900},{128,899},{175,900},{218,901},{251,896},{274,905},{293,895},{306,878},{307,853},{311,824},{317,800},{313,803},{322,782}}),
        Hand({{75,1140},{110,1124},{153,1100},{190,1075},{219,1062},{234,1070},{225,1080},{217,1073},{235,1052},{265,1034},{303,1014},{345,996},{390,986},{435,987},{480,1000},{520,1025},{551,1056},{568,1091},{570,1124}})};
    for(int column=0;column<2;++column)for(auto mark:examples){Translate(mark,{float(column*720),0});if(column==0){mark.tool=Tool::Pen;mark.pen_smoothing=PenSmoothing::None;}doc.Add(mark);}
    Label(doc,{30,435},L"Large U + 20 px local bulges");Label(doc,{30,720},L"S curve + small corrections");Label(doc,{30,935},L"Sharp turn + terminal jitter");Label(doc,{750,1175},L"Broad bend + short backtrack");
    Renderer renderer;SavePng(renderer.Flatten(frame,doc,frame.bounds),L"docs/hand-arrow-fit-preview.png");
}
}
int main(int argc,char** argv){if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 2;try{
    HeadConnectionRegression();HeadConnectionEdges();
    auto noisy=Noisy();const auto original=noisy.points;noisy.arrow_head=ArrowHead::None;const auto curve=BuildArrow(noisy).front().points;
    float deviation=0;Point worst{};for(auto p:curve){const float error=std::abs(p.y-(120+35*std::sin((p.x-40)/300*3.14159265f)));if(error>deviation){deviation=error;worst=p;}}
    std::cout<<"Shallow sine: deviation "<<deviation<<" px at "<<worst.x<<","<<worst.y<<"\n";
    Expect(Turning(curve)<Turning(original)*.25f,"uneven-density mouse jitter loses at least 75 percent of total turning");
    Expect(deviation<5,"smoothed curve remains within five pixels of intended broad bend");
    Expect(curve.front()==noisy.a&&curve.back()==noisy.b&&noisy.points==original,"smoothing preserves endpoints and raw samples");
    auto bump=BroadBump();bump.arrow_head=ArrowHead::None;const auto fitted=BuildArrow(bump).front().points;float radial_error=0,bottom=0;
    for(const auto p:fitted){radial_error=std::max(radial_error,std::abs(Distance(p,{360,100})-300));bottom=std::max(bottom,p.y);}
    std::cout<<"Broad U: curvature reversals "<<Reversals(bump.points)<<" -> "<<Reversals(fitted)<<", radial deviation "<<radial_error<<" px\n";
    Expect(Reversals(bump.points)>=4&&Reversals(fitted)<=2,"overall fit removes extra curvature reversals caused by broad local bulges");
    Expect(radial_error<18&&bottom>380&&fitted.front()==bump.a&&fitted.back()==bump.b,"overall fit keeps the intended large U within three percent of its span");
    auto end=Hand({{40,100},{75,100},{115,100},{150,100},{180,100},{205,100},{225,100},{223,102}});
    for(auto head:{ArrowHead::Filled,ArrowHead::Hollow}){end.arrow_head=head;const auto paths=BuildArrow(end);const auto& shaft=paths.front().points;const auto& polygon=paths.back().points;
        const Point base{(polygon[1].x+polygon[2].x)*.5f,(polygon[1].y+polygon[2].y)*.5f};const Point direction{end.b.x-base.x,end.b.y-base.y};
        Expect(polygon.front()==end.b,"head tip stays at the released pointer endpoint");Expect(direction.x>20&&std::abs(direction.y)<8,"small backward terminal jitter does not reverse the head");
        const auto tail=shaft.back();const float lateral=std::abs((tail.x-end.b.x)*direction.y-(tail.y-end.b.y)*direction.x)/std::hypot(direction.x,direction.y);
        Expect(lateral<.1f,"solid and hollow shafts connect on the arrowhead axis");
        const auto frame=MakeFrame({0,0,270,170},0xffffffff);Document doc;doc.Add(end);Renderer renderer;const auto rendered=renderer.Flatten(frame,doc,frame.bounds);
        const auto from=shaft[shaft.size()-2];bool connected=true;for(int i=0;i<=20;++i){const float t=float(i)/20;const Point p{from.x+(base.x-from.x)*t,from.y+(base.y-from.y)*t};bool ink=false;for(int y=int(p.y)-2;y<=int(p.y)+2;++y)for(int x=int(p.x)-2;x<=int(p.x)+2;++x)if(x>=0&&x<270&&y>=0&&y<170)ink=ink||rendered.pixels[size_t(y)*270+size_t(x)]!=0xffffffff;connected=connected&&ink;}Expect(connected,"rendered shaft meets the arrowhead without a visible gap");
    }
    auto loop=Hand({{100,100},{140,80},{170,100},{170,140},{130,160},{95,140},{100,100}});const auto closed=BuildArrow(loop);Expect(closed.size()==2&&closed.front().points.size()>20,"closed trajectory with identical endpoints remains drawable");
    auto repeated=Hand({{10,10},{10,10},{10,10},{40,40},{40,40},{60,10},{60,10}});bool finite=true;for(const auto& path:BuildArrow(repeated))for(auto p:path.points)finite=finite&&std::isfinite(p.x)&&std::isfinite(p.y);Expect(finite&&!BuildArrow(repeated).empty(),"repeated samples produce finite drawable geometry");
    Document doc;doc.Add(noisy);doc.Checkpoint();Translate(doc.marks.front(),{15,20});const auto edited=doc.marks.front();doc.Undo();Expect(doc.marks.front()==noisy,"editing undo restores the entire original mouse trajectory");doc.Redo();Expect(doc.marks.front()==edited,"redo restores translated samples without replacing them by smoothing output");
    const auto moved_end=EditMark(end,EditPart::Whole,25,end.b,{240,130},false,false);
    Expect(moved_end.points.back()==moved_end.b&&BuildArrow(moved_end).back().points.front()==moved_end.b,"endpoint editing updates raw endpoint and rendered arrow tip together");
    const auto moved_start=EditMark(end,EditPart::Whole,24,end.a,{20,80},false,false);
    Expect(moved_start.points.front()==moved_start.a&&BuildArrow(moved_start).front().points.front()==moved_start.a,"start editing updates raw start and rendered shaft together");
    std::vector<Point> long_points;for(int i=0;i<20000;++i)long_points.push_back({float(i),100+30*std::sin(float(i)/30)});
    const auto long_mark=Hand(std::move(long_points));const auto begin=std::chrono::steady_clock::now();const auto bounded=BuildArrow(long_mark);
    std::cout<<"20,000 samples: "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()<<" ms\n";
    Expect(!bounded.empty()&&bounded.front().points.size()<=4200,"long mouse streams have a bounded output geometry budget");
    if(argc>1&&std::string_view(argv[1])=="--connection-preview")ConnectionPreview();
    if(argc>1&&std::string_view(argv[1])=="--preview"){Preview();FitPreview();}
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';++failures;}CoUninitialize();return failures?1:0;}
