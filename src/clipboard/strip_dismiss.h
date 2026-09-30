#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

namespace lumashot::clipboard {
// Drop target that hides the folded strip: while the strip is dragged, a round
// close target fades in near the bottom centre of the pointer's work area. Screen
// geometry is in physical pixels; the constants below are DIP.
struct DismissZone {
    RECT work{};float scale{1};
    // Keep the magnetised strip (96 DIP tall) clear of LiquidDetachDistance so it
    // does not start docking to the bottom edge while it sits in the target.
    static constexpr float CenterLift=112,RestRadius=28,ArmedRadius=36;
    static constexpr float ArmDistance=64,DisarmDistance=88,ReachDistance=260,RevealTravel=14;
    static constexpr float WindowWidth=240,WindowHeight=236;
    float Scale()const{return std::max(.25f,scale);}
    POINT Center()const{
        const LONG lift=std::min(static_cast<LONG>(std::lround(CenterLift*Scale())),std::max(0L,(work.bottom-work.top)/2));
        return {work.left+(work.right-work.left)/2,work.bottom-lift};
    }
    // Pointer distance from the target centre, in DIP.
    float Distance(POINT p)const{const auto c=Center();return static_cast<float>(std::hypot(double(p.x-c.x),double(p.y-c.y)))/Scale();}
    RECT Window()const{
        const auto c=Center();
        const LONG w=static_cast<LONG>(std::lround(WindowWidth*Scale())),h=static_cast<LONG>(std::lround(WindowHeight*Scale()));
        return {c.x-w/2,std::max(work.top,work.bottom-h),c.x-w/2+w,work.bottom};
    }
    bool operator==(const DismissZone& other)const{return EqualRect(&work,&other.work)&&scale==other.scale;}
};
// Pure drag-state logic, kept free of windows so tests can drive it directly.
class DismissTracker {
    DismissZone zone_{};POINT start_{};bool began_{},active_{},armed_{},blocked_{};float proximity_{};
public:
    void Begin(POINT start){*this={};start_=start;began_=true;}
    void Reset(){*this={};}
    // Returns whether releasing now hides the strip. The target only appears once
    // the pointer has travelled a little, and a drag that starts inside the zone
    // (a strip docked at the bottom centre) cannot arm until it has left it once.
    bool Update(POINT pointer,RECT work,float scale){
        if(!began_)return false;
        zone_={work,scale};
        if(!active_){
            const float travel=static_cast<float>(std::hypot(double(pointer.x-start_.x),double(pointer.y-start_.y)))/zone_.Scale();
            if(travel<DismissZone::RevealTravel)return false;
            active_=true;blocked_=zone_.Distance(start_)<=DismissZone::DisarmDistance;
        }
        const float d=zone_.Distance(pointer);
        if(blocked_&&d>DismissZone::DisarmDistance)blocked_=false;
        armed_=!blocked_&&(armed_?d<=DismissZone::DisarmDistance:d<=DismissZone::ArmDistance);
        proximity_=armed_?1.f:std::clamp(1.f-(d-DismissZone::ArmDistance)/(DismissZone::ReachDistance-DismissZone::ArmDistance),0.f,1.f);
        return armed_;
    }
    bool Active()const{return active_;}
    bool Armed()const{return armed_;}
    float Proximity()const{return proximity_;}
    const DismissZone& Zone()const{return zone_;}
    // Strip bounds centred in the target, kept inside the work area.
    RECT Magnet(LONG w,LONG h)const{
        const auto c=zone_.Center();const auto& r=zone_.work;
        const LONG x=std::clamp(c.x-w/2,r.left,std::max(r.left,r.right-w)),y=std::clamp(c.y-h/2,r.top,std::max(r.top,r.bottom-h));
        return {x,y,x+w,y+h};
    }
};
// The visible target: a click-through layered window. It animates only while it
// fades, reacts to arming, or absorbs the strip; otherwise no timer runs, and it
// frees its window and renderer as soon as it is hidden.
class DismissTarget {
public:
    explicit DismissTarget(HWND owner);
    ~DismissTarget();
    DismissTarget(const DismissTarget&)=delete;
    DismissTarget& operator=(const DismissTarget&)=delete;
    // Shows (or moves) the target just below `above` in the topmost band.
    void Show(const DismissZone& zone,bool dark,HWND above);
    void SetState(bool armed,float proximity);
    void Hide();
    // Plays the absorb animation with a snapshot of the strip (premultiplied BGRA,
    // top-down, `screen` is its window rect), then releases everything.
    void Dismiss(const uint32_t* pixels,int width,int height,RECT screen);
    void Reset();
    HWND Window()const;
    bool Visible()const;
    bool Animating()const;
private:
    friend struct DismissTargetTest;
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
