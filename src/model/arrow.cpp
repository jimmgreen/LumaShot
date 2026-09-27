#include "model/arrow.h"
#include <algorithm>
#include <cmath>
namespace lumashot {
namespace {
Point Mix(Point a,Point b,float t){return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};}
float Distance(Point a,Point b){return std::hypot(b.x-a.x,b.y-a.y);}
Point Unit(Point v){const float n=std::hypot(v.x,v.y);return n>.001f?Point{v.x/n,v.y/n}:Point{1,0};}
Point Bezier(Point a,Point b,Point c,Point d,float t){return Mix(Mix(Mix(a,b,t),Mix(b,c,t),t),Mix(Mix(b,c,t),Mix(c,d,t),t),t);}
float PathLength(const std::vector<Point>& points){float length=0;for(size_t i=1;i<points.size();++i)length+=Distance(points[i-1],points[i]);return length;}
float Dot(Point a,Point b){return a.x*b.x+a.y*b.y;}
Point Sub(Point a,Point b){return {a.x-b.x,a.y-b.y};}
std::vector<Point> SimplifyIntent(const std::vector<Point>& points,float tolerance){
    std::vector<bool> keep(points.size());keep.front()=keep.back()=true;
    std::vector<std::pair<size_t,size_t>> pending{{0,points.size()-1}};
    while(!pending.empty()){const auto [first,last]=pending.back();pending.pop_back();
        const auto axis=Sub(points[last],points[first]);const float squared=Dot(axis,axis);float worst=tolerance*tolerance;size_t split=last;
        for(size_t i=first+1;i<last;++i){const auto delta=Sub(points[i],points[first]);const float t=squared>.001f?std::clamp(Dot(delta,axis)/squared,0.f,1.f):0.f;
            const auto error=Sub(points[i],Mix(points[first],points[last],t));const float e=Dot(error,error);if(e>worst){worst=e;split=i;}}
        if(split!=last){keep[split]=true;pending.emplace_back(first,split);pending.emplace_back(split,last);}
    }
    std::vector<Point> result;for(size_t i=0;i<points.size();++i)if(keep[i])result.push_back(points[i]);return result;
}
// Fit a whole interval with one cubic. Adjacent intervals share their tangent,
// rather than placing an interpolating knot at every mouse sample.
void FitCurve(const std::vector<Point>& points,size_t first,size_t last,Point forward,Point backward,float tolerance,int depth,std::vector<Point>& output){
    const auto a=points[first],d=points[last];
    std::vector<float> parameters(last-first+1);float length=0;
    for(size_t i=first+1;i<=last;++i){length+=Distance(points[i-1],points[i]);parameters[i-first]=length;}
    if(length<.001f){output.push_back(d);return;}
    for(auto& t:parameters)t/=length;
    Point b{},c{};
    for(int iteration=0;iteration<4;++iteration){
    float c00=0,c01=0,c11=0,x0=0,x1=0;
    for(size_t i=first;i<=last;++i){const float t=parameters[i-first],u=1-t;
        const float b0=u*u*u,b1=3*t*u*u,b2=3*t*t*u,b3=t*t*t;
        const Point v0{forward.x*b1,forward.y*b1},v1{backward.x*b2,backward.y*b2};
        const Point residual{points[i].x-a.x*(b0+b1)-d.x*(b2+b3),points[i].y-a.y*(b0+b1)-d.y*(b2+b3)};
        c00+=Dot(v0,v0);c01+=Dot(v0,v1);c11+=Dot(v1,v1);x0+=Dot(v0,residual);x1+=Dot(v1,residual);
    }
    const float determinant=c00*c11-c01*c01;float left=length/3,right=left;
    if(std::abs(determinant)>1e-6f){left=(x0*c11-x1*c01)/determinant;right=(c00*x1-c01*x0)/determinant;}
    if(left<length*.01f||right<length*.01f||left>length||right>length){left=right=length/3;}
    b={a.x+forward.x*left,a.y+forward.y*left};c={d.x+backward.x*right,d.y+backward.y*right};
    if(iteration==3)break;
    // Closest-point reparameterization prevents chord parameters from shrinking
    // shallow arches when the endpoint tangents constrain the cubic handles.
    auto refined=parameters;
    for(size_t i=1;i+1<parameters.size();++i){const float t=parameters[i],u=1-t;
        const auto residual=Sub(Bezier(a,b,c,d,t),points[first+i]);
        const Point velocity{3*(u*u*(b.x-a.x)+2*u*t*(c.x-b.x)+t*t*(d.x-c.x)),3*(u*u*(b.y-a.y)+2*u*t*(c.y-b.y)+t*t*(d.y-c.y))};
        const Point acceleration{6*(u*(c.x-2*b.x+a.x)+t*(d.x-2*c.x+b.x)),6*(u*(c.y-2*b.y+a.y)+t*(d.y-2*c.y+b.y))};
        const float denominator=Dot(velocity,velocity)+Dot(residual,acceleration);
        if(denominator>1e-6f)refined[i]=std::clamp(t-Dot(residual,velocity)/denominator,(parameters[i-1]+t)*.5f,(t+parameters[i+1])*.5f);
    }
    parameters=std::move(refined);
    }
    float error=0;size_t split=(first+last)/2;
    for(size_t i=first+1;i<last;++i){const float e=Distance(Bezier(a,b,c,d,parameters[i-first]),points[i]);if(e>error){error=e;split=i;}}
    if(error>tolerance&&last-first>3&&depth<6){
        // Avoid tiny one-sample intervals and derive a stable common direction.
        split=std::clamp(split,first+2,last-2);
        const size_t reach=std::min(size_t(3),std::min(split-first,last-split));
        const auto tangent=Unit(Sub(points[split+reach],points[split-reach]));
        FitCurve(points,first,split,forward,{-tangent.x,-tangent.y},tolerance,depth+1,output);
        FitCurve(points,split,last,tangent,backward,tolerance,depth+1,output);
        return;
    }
    const int steps=std::clamp(static_cast<int>(std::ceil((Distance(a,b)+Distance(b,c)+Distance(c,d))/2)),8,64);
    for(int i=1;i<=steps;++i)output.push_back(Bezier(a,b,c,d,float(i)/float(steps)));
}
std::vector<Point> HandCurve(const Mark& mark){
    std::vector<Point> raw{mark.a};
    for(size_t i=1;i<mark.points.size();++i){const auto p=mark.points[i];if(std::isfinite(p.x)&&std::isfinite(p.y)&&Distance(raw.back(),p)>.01f)raw.push_back(p);}
    if(Distance(raw.back(),mark.b)>.01f)raw.push_back(mark.b);else raw.back()=mark.b;
    const float length=PathLength(raw);if(length<.01f)return {};
    const size_t intervals=static_cast<size_t>(std::clamp(std::ceil(length/5.f),1.f,1024.f));
    std::vector<Point> samples;samples.reserve(intervals+1);samples.push_back(raw.front());
    size_t segment=1;float before=0;
    for(size_t i=1;i<intervals;++i){const float at=length*float(i)/float(intervals);
        while(segment+1<raw.size()&&before+Distance(raw[segment-1],raw[segment])<at){before+=Distance(raw[segment-1],raw[segment]);++segment;}
        const float span=Distance(raw[segment-1],raw[segment]);samples.push_back(Mix(raw[segment-1],raw[segment],span>.001f?(at-before)/span:0));}
    samples.push_back(raw.back());
    Point low=mark.a,high=mark.a;for(auto p:samples){low.x=std::min(low.x,p.x);low.y=std::min(low.y,p.y);high.x=std::max(high.x,p.x);high.y=std::max(high.y,p.y);}
    const float scale=Distance(low,high);
    // Remove small corrections before fitting: otherwise a local backtrack can
    // become an unwanted spline loop even when its positional error is tiny.
    const auto endDirection=[&](bool front){
        const auto tip=front?samples.front():samples.back();
        const float window=std::min(length*.4f,std::clamp(scale*.12f,24.f,72.f));
        // Fit a local quadratic in distance from the endpoint. Its derivative estimates the
        // endpoint tangent without the curvature bias of a long endpoint chord.
        float s2=0,s3=0,s4=0;Point r1{},r2{},delta{};
        for(size_t i=1;i<samples.size();++i){const auto p=front?samples[i]:samples[samples.size()-1-i];
            const float radial=Distance(tip,p);const float t=radial/std::max(window,.001f);delta=Sub(p,tip);
            s2+=t*t;s3+=t*t*t;s4+=t*t*t*t;
            r1.x+=t*delta.x;r1.y+=t*delta.y;r2.x+=t*t*delta.x;r2.y+=t*t*delta.y;
            if(radial>=window)break;
        }
        const auto chord=Unit(delta);const float determinant=s2*s4-s3*s3;
        if(determinant>1e-6f){const Point derivative{(r1.x*s4-r2.x*s3)/determinant,(r1.y*s4-r2.y*s3)/determinant};
            const auto direction=Unit(derivative);if(Dot(direction,chord)>.5f)return direction;}
        return chord;
    };
    const auto forward=endDirection(true),backward=endDirection(false);
    auto intent=SimplifyIntent(samples,std::min(scale*.2f,std::clamp(scale*.008f,1.5f,8.f)));
    if(PathLength(intent)<.01f)intent=samples;
    const float cleanLength=PathLength(intent);if(cleanLength<.01f)return {};
    samples.front()=intent.front();samples.back()=intent.back();segment=1;before=0;
    for(size_t i=1;i<intervals;++i){const float at=cleanLength*float(i)/float(intervals);
        while(segment+1<intent.size()&&before+Distance(intent[segment-1],intent[segment])<at){before+=Distance(intent[segment-1],intent[segment]);++segment;}
        const float span=Distance(intent[segment-1],intent[segment]);samples[i]=Mix(intent[segment-1],intent[segment],span>.001f?(at-before)/span:0);}
    const float spacing=cleanLength/float(intervals);
    const float radius=std::clamp(scale*.08f,16.f,56.f);
    const int reach=std::clamp(static_cast<int>(std::ceil(radius/spacing)),1,16);
    auto filtered=samples;
    for(size_t i=1;i<intervals;++i){Point sum{};float weight=0;
        for(int k=-reach;k<=reach;++k){const int index=static_cast<int>(i)+k;
            // Linear endpoint extension avoids flattening the start/end tangent.
            Point p;if(index<0){const auto delta=Sub(samples[1],samples[0]);p={samples[0].x+delta.x*float(index),samples[0].y+delta.y*float(index)};}
            else if(index>static_cast<int>(intervals)){const auto delta=Sub(samples.back(),samples[intervals-1]);p={samples.back().x+delta.x*float(index-static_cast<int>(intervals)),samples.back().y+delta.y*float(index-static_cast<int>(intervals))};}
            else p=samples[static_cast<size_t>(index)];
            const float normalized=float(k)/float(reach);const float w=std::exp(-normalized*normalized*3);
            sum.x+=p.x*w;sum.y+=p.y*w;weight+=w;}
        filtered[i]={sum.x/weight,sum.y/weight};}
    std::vector<Point> curve;curve.reserve(256);curve.push_back(mark.a);
    FitCurve(filtered,0,intervals,forward,backward,std::clamp(scale*.055f,4.f,36.f),0,curve);
    curve.back()=mark.b;return curve;
}
void JoinHead(std::vector<Point>& shaft,Point attachment,float angle,bool front,float window){
    if(shaft.size()<2)return;
    if(front)std::reverse(shaft.begin(),shaft.end());
    size_t anchor=shaft.size()-1;float length=0;
    while(anchor>0&&length<window){length+=Distance(shaft[anchor],shaft[anchor-1]);--anchor;}
    const Point a=shaft[anchor];const float span=Distance(a,attachment);
    if(span>.01f){
        const Point direction=anchor>0?Unit({a.x-shaft[anchor-1].x,a.y-shaft[anchor-1].y}):Unit({attachment.x-a.x,attachment.y-a.y});
        const float handle=span/3;
        const Point b{a.x+direction.x*handle,a.y+direction.y*handle},c{attachment.x-std::cos(angle)*handle,attachment.y-std::sin(angle)*handle};
        shaft.resize(anchor+1);for(int i=1;i<=16;++i)shaft.push_back(Bezier(a,b,c,attachment,float(i)/16));
    }else shaft.back()=attachment;
    if(front)std::reverse(shaft.begin(),shaft.end());
}
}
std::vector<ArrowPath> BuildArrow(const Mark& mark){
    std::vector<ArrowPath> result;
    const bool hand=mark.arrow_type==ArrowType::HandDrawn&&mark.points.size()>1;
    const float distance=Distance(mark.a,mark.b);if(!hand&&distance<.01f)return result;
    const bool both=mark.arrow_type==ArrowType::Double||mark.arrow_style==ArrowStyle::Double;
    const auto head=mark.arrow_head;
    std::vector<Point> shaft;
    if(hand)shaft=HandCurve(mark);
    else if(mark.arrow_type==ArrowType::Curved||mark.arrow_type==ArrowType::HandDrawn){
        const Point normal{-(mark.b.y-mark.a.y)/distance,(mark.b.x-mark.a.x)/distance};
        for(int i=0;i<=32;++i){const float t=float(i)/32;const float bend=mark.arrow_type==ArrowType::Curved?-distance*.7f*t*(1-t):std::sin(t*6.2831853f)*distance*.08f;const auto p=Mix(mark.a,mark.b,t);shaft.push_back({p.x+normal.x*bend,p.y+normal.y*bend});}
    }else shaft={mark.a,mark.b};
    const float extent=hand?PathLength(shaft):distance;if(shaft.size()<2||extent<.01f)return result;
    const float size=std::min(std::max(1.f,mark.arrow_size),extent*(both?.45f:.8f));
    const bool open=mark.arrow_style==ArrowStyle::Open||head==ArrowHead::Arc;
    const bool connect_open=hand&&open&&head!=ArrowHead::None&&head!=ArrowHead::Circle;
    const float length=head==ArrowHead::Short?size*.55f:(head==ArrowHead::Slender?size*1.2f:size);
    const auto tangent=[&](bool start){
        const Point tip=start?shaft.front():shaft.back();
        // For open heads use the chord across the visible connection footprint,
        // not a subpixel final segment: that segment is too local on a tight bend
        // and can orient the head to one side of the incoming stroke.
        const float reach=connect_open?std::min(length*.75f,extent*(both?.18f:.25f)):0;
        float traveled=0;Point previous=tip;
        for(size_t i=1;i<shaft.size();++i){const auto p=start?shaft[i]:shaft[shaft.size()-1-i];const float span=Distance(previous,p);
            if(span>.001f&&traveled+span>=reach&&Distance(tip,p)>.01f){const Point at=reach>0?Mix(previous,p,std::clamp((reach-traveled)/span,0.f,1.f)):p;return std::atan2(tip.y-at.y,tip.x-at.x);}
            traveled+=span;previous=p;
        }return 0.f;
    };
    const float end_angle=tangent(false),start_angle=tangent(true);
    const float half=head==ArrowHead::Wide?size*.65f:(head==ArrowHead::Slender?size*.22f:size*.38f);
    const float inset=head==ArrowHead::None||open?0.f:(head==ArrowHead::Diamond?length*.8f:(head==ArrowHead::Circle?size*.65f:length*.9f));
    const auto trim=[&](bool front){
        if(head==ArrowHead::Hollow&&!open){
            // Intersect with the head's rear plane, not an arc-length offset:
            // a curved shaft travels farther than its projection onto the head.
            // Reserve half the stroke width for its round end cap as well.
            const Point tip=front?mark.a:mark.b;const float angle=front?start_angle:end_angle;
            const auto projection=[&](Point p){return (p.x-tip.x)*std::cos(angle)+(p.y-tip.y)*std::sin(angle);};
            const float limit=-length-std::max(0.f,mark.width)*.5f;
            while(shaft.size()>1){
                const size_t a=front?0:shaft.size()-1,b=front?1:shaft.size()-2;
                const float pa=projection(shaft[a]),pb=projection(shaft[b]);
                if(pa<=limit)break;
                if(pb<=limit){shaft[a]=Mix(shaft[a],shaft[b],(pa-limit)/(pa-pb));break;}
                if(front)shaft.erase(shaft.begin());else shaft.pop_back();
            }
            return;
        }
        float remaining=inset;while(shaft.size()>1){const size_t a=front?0:shaft.size()-1,b=front?1:shaft.size()-2;const float segment=Distance(shaft[a],shaft[b]);if(segment>remaining){shaft[a]=Mix(shaft[a],shaft[b],remaining/segment);break;}remaining-=segment;if(front)shaft.erase(shaft.begin());else shaft.pop_back();}
    };
    trim(false);if(both)trim(true);
    if(hand&&inset>0){const float offset=head==ArrowHead::Hollow?length+std::max(0.f,mark.width)*.5f:inset;
        JoinHead(shaft,{mark.b.x-std::cos(end_angle)*offset,mark.b.y-std::sin(end_angle)*offset},end_angle,false,std::max(12.f,length));
        if(both)JoinHead(shaft,{mark.a.x-std::cos(start_angle)*offset,mark.a.y-std::sin(start_angle)*offset},start_angle,true,std::max(12.f,length));}
    if(connect_open){
        // Sharing only the tip/tangent is not enough for an open head on a tight
        // bend. Remove lateral deviation inside the head's footprint, tapering
        // the correction to zero with a quintic C2 blend along the existing
        // curve. Preserve axial progression and the original sampling instead
        // of inserting an extra cubic that can create a small terminal bulge.
        const float collar=std::min(length*.75f,extent*(both?.18f:.25f));
        const float window=std::min(extent*(both?.45f:.8f),std::max(length*1.8f,collar+std::max(0.f,mark.width)*2));
        const auto connect=[&](bool front){
            const Point tip=front?mark.a:mark.b;const float angle=front?start_angle:end_angle;
            const Point axis{std::cos(angle),std::sin(angle)};Point previous=tip;float traveled=0;
            for(size_t i=1;i<shaft.size();++i){const size_t index=front?i:shaft.size()-1-i;const Point original=shaft[index];
                traveled+=Distance(previous,original);previous=original;if(traveled>=window)break;
                const float t=std::clamp((window-traveled)/std::max(.001f,window-collar),0.f,1.f);
                const float weight=t*t*t*(10+t*(-15+6*t));const auto delta=Sub(original,tip);const float axial=Dot(delta,axis);
                const Point centered{tip.x+axis.x*axial,tip.y+axis.y*axial};shaft[index]=Mix(original,centered,weight);
            }
        };
        connect(false);if(both)connect(true);
    }
    if(mark.arrow_type==ArrowType::Triangle&&shaft.size()>1){const auto end=shaft.back();const Point normal{-std::sin(end_angle),std::cos(end_angle)};const float width=std::min(size*.32f,std::max(mark.width*.5f,1.f));result.push_back({{mark.a,{end.x+normal.x*width,end.y+normal.y*width},{end.x-normal.x*width,end.y-normal.y*width}},true,true,true});}
    else if(shaft.size()>1)result.push_back({shaft,false,false,true});
    const auto circle=[&](Point center,float radius){ArrowPath path;path.filled=path.closed=true;for(int i=0;i<32;++i){const float a=float(i)*6.2831853f/32;path.points.push_back({center.x+radius*std::cos(a),center.y+radius*std::sin(a)});}result.push_back(std::move(path));};
    const auto add_head=[&](Point tip,float angle){
        if(head==ArrowHead::None)return;
        const auto point=[&](float x,float y){return Point{tip.x+std::cos(angle)*x-std::sin(angle)*y,tip.y+std::sin(angle)*x+std::cos(angle)*y};};
        if(head==ArrowHead::Circle){circle(point(-size*.35f,0),size*.35f);return;}
        if(open){ArrowPath path;path.points.push_back(point(-length,-half));if(head==ArrowHead::Arc){for(int i=1;i<=12;++i){const float t=float(i)/12;path.points.push_back(point(-length*(1-t)*(1-t),-half*(1-t)));}for(int i=1;i<=12;++i){const float t=float(i)/12;path.points.push_back(point(-length*t*t,half*t));}}else {path.points.push_back(tip);path.points.push_back(point(-length,half));}result.push_back(std::move(path));return;}
        std::vector<Point> polygon;
        if(head==ArrowHead::Diamond)polygon={tip,point(-length*.5f,-half),point(-length,0),point(-length*.5f,half)};
        else if(head==ArrowHead::Slanted)polygon={tip,point(-length,-half*.35f),point(-length*.65f,half),point(-length*.3f,half*.5f)};
        else polygon={tip,point(-length,-half),point(-length,half)};
        if(head==ArrowHead::Rounded){std::vector<Point> rounded;for(size_t i=0;i<polygon.size();++i){const auto p=polygon[i],a=Mix(p,polygon[(i+polygon.size()-1)%polygon.size()],.16f),b=Mix(p,polygon[(i+1)%polygon.size()],.16f);for(int j=0;j<=6;++j){const float t=float(j)/6;rounded.push_back(Mix(Mix(a,p,t),Mix(p,b,t),t));}}polygon=std::move(rounded);}
        result.push_back({std::move(polygon),head!=ArrowHead::Hollow,true,false});
    };
    add_head(mark.b,end_angle);if(both)add_head(mark.a,start_angle);
    if(mark.arrow_type==ArrowType::DotStart)circle(mark.a,std::max(mark.width,size*.3f));
    return result;
}
}
