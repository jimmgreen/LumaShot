#pragma once
#include <windows.h>
#include <d2d1.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace lumashot::clipboard {
inline float LiquidSmooth(float x) { x=std::clamp(x,0.f,1.f);return x*x*(3-2*x); }
struct LiquidSpring {
    float value{},velocity{};
    LiquidSpring At(float goal,float seconds,float frequency=29.f,float damping=.82f) const {
        if(seconds<=0)return *this;
        const float decay=std::exp(-damping*frequency*seconds);
        const float w=frequency*std::sqrt(1-damping*damping),a=value-goal;
        const float s=std::sin(w*seconds),c=std::cos(w*seconds);
        return {goal+decay*(a*c+(velocity+damping*frequency*a)*s/w),
            decay*(velocity*c-(damping*frequency*velocity+frequency*frequency*a)*s/w)};
    }
};
struct LiquidPose { D2D1_RECT_F bounds{};float open{}; };
inline D2D1_RECT_F LiquidRect(RECT r) {return {float(r.left),float(r.top),float(r.right),float(r.bottom)};}
inline RECT LiquidPixels(D2D1_RECT_F r) {
    return {LONG(std::lround(r.left)),LONG(std::lround(r.top)),LONG(std::lround(r.right)),LONG(std::lround(r.bottom))};
}
// Analytic damped springs: frame-rate independent, with velocity-preserving retargets.
class LiquidTween {
    std::array<LiquidSpring,5> origin_{};
    std::array<float,5> goal_{};
    ULONGLONG start_{},sampled_{};
    UINT duration_{350};bool fast_{};
    std::array<LiquidSpring,5> Values(ULONGLONG now) const {
        auto result=origin_;const float elapsed=float(now>start_?now-start_:0)/1000.f;
        for(size_t i=0;i<result.size();++i) {
            const float frequency=fast_?45.f:(i==1||i==3?25.f:31.f);
            result[i]=now>=start_+duration_?LiquidSpring{goal_[i],0}:origin_[i].At(goal_[i],elapsed,frequency);
        }
        return result;
    }
public:
    void Reset(RECT rect,float open,ULONGLONG now) {
        goal_={float(rect.left),float(rect.top),float(rect.right),float(rect.bottom),open};
        for(size_t i=0;i<goal_.size();++i)origin_[i]={goal_[i],0};
        start_=sampled_=now;duration_=0;
    }
    void Target(RECT rect,float open,ULONGLONG now,UINT duration=350,bool fast=false) {
        now=std::max(now,sampled_);origin_=Values(now);
        goal_={float(rect.left),float(rect.top),float(rect.right),float(rect.bottom),open};
        start_=sampled_=now;duration_=duration;fast_=fast;
    }
    LiquidPose Sample(ULONGLONG now) {
        sampled_=std::max(now,sampled_);const auto v=Values(sampled_);
        return {{v[0].value,v[1].value,v[2].value,v[3].value},std::clamp(v[4].value,0.f,1.f)};
    }
    ULONGLONG Started()const{return start_;}
    bool Done(ULONGLONG now)const{return now>=start_+duration_;}
};
class LiquidPull {
    LiquidSpring x_,y_;float tx_{},ty_{};ULONGLONG last_{},input_{};
public:
    void Reset(ULONGLONG now){x_=y_={};tx_=ty_=0;last_=input_=now;}
    D2D1_POINT_2F Sample(ULONGLONG now) {
        now=std::max(last_,now);
        if(now>input_+45)tx_=ty_=0;
        const float dt=float(now-last_)/1000.f;
        x_=x_.At(tx_,dt,34.f,.70f);y_=y_.At(ty_,dt,34.f,.70f);last_=now;
        return {x_.value,y_.value};
    }
    void Impulse(float vx,float vy,ULONGLONG now) {
        Sample(now);input_=now;tx_=std::clamp(vx*.008f,-7.f,7.f);ty_=std::clamp(vy*.008f,-9.f,9.f);
    }
    void Release(ULONGLONG now){Sample(now);tx_=ty_=0;input_=0;}
    void Recoil(float vx,float vy,ULONGLONG now){
        Sample(now);x_.velocity=std::clamp(x_.velocity+vx,-220.f,220.f);
        y_.velocity=std::clamp(y_.velocity+vy,-220.f,220.f);
    }
    bool Settled()const{return std::abs(x_.value)+std::abs(y_.value)<.035f&&std::abs(x_.velocity)+std::abs(y_.velocity)<.25f;}
};
enum class LiquidEdge {None,Left,Top,Right,Bottom};
struct LiquidDock {LiquidEdge edge{LiquidEdge::None};float strength{};};
inline constexpr float LiquidDetachDistance=44.f;
inline float LiquidEdgeGap(D2D1_RECT_F r,RECT work,LiquidEdge edge){
    switch(edge){
    case LiquidEdge::Left:return r.left-work.left;
    case LiquidEdge::Top:return r.top-work.top;
    case LiquidEdge::Right:return work.right-r.right;
    case LiquidEdge::Bottom:return work.bottom-r.bottom;
    default:return LiquidDetachDistance;
    }
}
inline LiquidDock FindLiquidDock(D2D1_RECT_F r,RECT work,float scale) {
    const std::array<float,4> gaps{r.left-work.left,r.top-work.top,work.right-r.right,work.bottom-r.bottom};
    const auto it=std::min_element(gaps.begin(),gaps.end());
    const float strength=LiquidSmooth(1-std::max(0.f,*it)/(LiquidDetachDistance*std::max(.25f,scale)));
    return strength>.001f?LiquidDock{static_cast<LiquidEdge>(1+std::distance(gaps.begin(),it)),strength}:LiquidDock{};
}
inline D2D1_RECT_F ConstrainLiquidBounds(D2D1_RECT_F r,RECT work) {
    const float w=std::clamp(r.right-r.left,1.f,float(std::max(1L,work.right-work.left)));
    const float h=std::clamp(r.bottom-r.top,1.f,float(std::max(1L,work.bottom-work.top)));
    r.left=std::clamp(r.left,float(work.left),float(work.right)-w);
    r.top=std::clamp(r.top,float(work.top),float(work.bottom)-h);
    r.right=r.left+w;r.bottom=r.top+h;return r;
}
inline RECT LiquidViewport(D2D1_RECT_F body,RECT work,float scale,bool dock=true) {
    const float pad=18*scale;
    RECT r{LONG(std::floor(body.left-pad)),LONG(std::floor(body.top-pad)),LONG(std::ceil(body.right+pad)),LONG(std::ceil(body.bottom+pad))};
    const auto edge=dock?FindLiquidDock(body,work,scale).edge:LiquidEdge::None;
    if(edge==LiquidEdge::Left)r.left=work.left;
    if(edge==LiquidEdge::Right)r.right=work.right;
    if(edge==LiquidEdge::Top)r.top=work.top;
    if(edge==LiquidEdge::Bottom)r.bottom=work.bottom;
    r.left=std::max(r.left,work.left);r.top=std::max(r.top,work.top);
    r.right=std::min(r.right,work.right);r.bottom=std::min(r.bottom,work.bottom);return r;
}
inline bool LiquidBudget(RECT from,RECT to,bool enabled) {
    const long long w=std::max(from.right,to.right)-std::min(from.left,to.left);
    const long long h=std::max(from.bottom,to.bottom)-std::min(from.top,to.top);
    return enabled&&w>0&&h>0&&w*h<=3000000;
}
}
