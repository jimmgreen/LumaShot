#pragma once
#include "model/document.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace lumashot::ui {
struct ToolbarMotionFrame {
    Box indicator{};
    std::array<float,17> hover{},press{};
    bool ready{},effects{};
};
// No HWND, wall clock or hit-test geometry is owned here. All rectangles are
// local to the toolbar so moving the whole panel never creates a flying pill.
class ToolbarMotion {
    struct Fade {
        float from{},goal{};uint64_t start{},duration{140};
        float At(uint64_t now)const{
            const float t=std::clamp(float(now>=start?now-start:0)/float(duration),0.f,1.f);
            return from+(goal-from)*t*t*(3-2*t);
        }
        void Set(float value,uint64_t now,bool animate,uint64_t ms){
            if(!animate){from=goal=value;start=now;return;}
            if(goal==value)return;
            from=At(now);goal=value;start=now;duration=ms;
        }
        bool Active(uint64_t now)const{return from!=goal&&now<start+duration;}
    };
    std::array<Box,17> boxes_{};
    std::array<Fade,17> hover_{},press_{};
    Box from_{},span_{},goal_{};uint64_t start_{};
    int selected_{-1};bool ready_{},moving_{},effects_{};
    static bool Valid(Box b){return b.right>b.left&&b.bottom>b.top;}
    static bool Near(Box a,Box b){return std::abs(a.left-b.left)<.05f&&std::abs(a.top-b.top)<.05f&&std::abs(a.right-b.right)<.05f&&std::abs(a.bottom-b.bottom)<.05f;}
    static bool ToolSlot(int id){return (id>=1&&id<=6)||id==14;}
    static Box Mix(Box a,Box b,float t){return {a.left+(b.left-a.left)*t,a.top+(b.top-a.top)*t,a.right+(b.right-a.right)*t,a.bottom+(b.bottom-a.bottom)*t};}
    Box Indicator(uint64_t now)const{
        if(!moving_||now>=start_+365)return goal_;
        const float elapsed=float(now>=start_?now-start_:0);
        if(elapsed<115){const float t=elapsed/115;return Mix(from_,span_,1-(1-t)*(1-t)*(1-t));}
        const float t=(elapsed-115)/250-1;
        // Modest overshoot; never distorts text, icons or clickable slots.
        return Mix(span_,goal_,1+1.55f*t*t*t+.55f*t*t);
    }
public:
    void Reset(){*this=ToolbarMotion{};}
    void Update(const std::array<Box,17>& boxes,int selected,int hovered,int pressed,uint64_t now,bool effects){
        bool relayout=!ready_;
        for(size_t i=0;i<boxes.size();++i)relayout|=!Near(boxes_[i],boxes[i]);
        const Box current=Indicator(now);const int previous=selected_;
        boxes_=boxes;effects_=effects;
        ready_=selected>=0&&selected<17&&Valid(boxes[size_t(selected)]);
        if(!ready_){Reset();return;}
        const Box next=boxes[size_t(selected)];
        if(relayout||!effects){from_=span_=goal_=next;moving_=false;}
        else if(selected!=previous){
            from_=current;goal_=next;start_=now;
            moving_=ToolSlot(previous)&&ToolSlot(selected)&&std::abs(current.top-next.top)<.5f&&std::abs(current.bottom-next.bottom)<.5f;
            span_={std::min(current.left,next.left),next.top,std::max(current.right,next.right),next.bottom};
            if(!moving_)from_=span_=goal_;
        }
        selected_=selected;
        for(size_t i=0;i<boxes.size();++i){
            hover_[i].Set(Valid(boxes[i])&&int(i)==hovered?1.f:0.f,now,effects&&!relayout,140);
            press_[i].Set(Valid(boxes[i])&&int(i)==pressed?1.f:0.f,now,effects&&!relayout,int(i)==pressed?70:180);
        }
    }
    ToolbarMotionFrame Sample(uint64_t now)const{
        ToolbarMotionFrame result;result.ready=ready_;result.effects=effects_;result.indicator=Indicator(now);
        for(size_t i=0;i<hover_.size();++i){result.hover[i]=hover_[i].At(now);result.press[i]=press_[i].At(now);}
        return result;
    }
    bool Active(uint64_t now)const{
        if(!ready_||!effects_)return false;
        if(moving_&&now<start_+365)return true;
        for(size_t i=0;i<hover_.size();++i)if(hover_[i].Active(now)||press_[i].Active(now))return true;
        return false;
    }
};
}
