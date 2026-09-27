#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>

namespace lumashot::ui {
template<std::size_t N> struct InteractionFrame {
    std::array<float,N> hover{},press{},selected{};
    bool ready{},effects{};
};
// Pure time sampling: no timers, window geometry, capture or background work.
template<std::size_t N> class InteractionMotion {
    struct Fade {
        float from{},goal{};uint64_t start{},duration{140};
        float At(uint64_t now)const {
            const float t=std::clamp(float(now>=start?now-start:0)/float(duration),0.f,1.f);
            return from+(goal-from)*t*t*(3-2*t);
        }
        void Set(float value,uint64_t now,bool animate,uint64_t ms) {
            if(!animate){from=goal=value;start=now;return;}
            if(goal==value)return;
            from=At(now);goal=value;start=now;duration=ms;
        }
        bool Active(uint64_t now)const{return from!=goal&&now<start+duration;}
    };
    std::array<Fade,N> hover_{},press_{},selected_{};
    bool ready_{},effects_{};
public:
    void Reset(){*this=InteractionMotion{};}
    void Update(int hover,int press,const std::array<bool,N>& selected,uint64_t now,bool effects) {
        const bool animate=ready_&&effects;effects_=effects;ready_=true;
        for(std::size_t i=0;i<N;++i){
            hover_[i].Set(int(i)==hover?1.f:0.f,now,animate,140);
            press_[i].Set(int(i)==press?1.f:0.f,now,animate,int(i)==press?70:180);
            selected_[i].Set(selected[i]?1.f:0.f,now,animate,180);
        }
    }
    InteractionFrame<N> Sample(uint64_t now)const {
        InteractionFrame<N> result;result.ready=ready_;result.effects=effects_;
        for(std::size_t i=0;i<N;++i){result.hover[i]=hover_[i].At(now);result.press[i]=press_[i].At(now);result.selected[i]=selected_[i].At(now);}
        return result;
    }
    bool Active(uint64_t now)const {
        if(!ready_||!effects_)return false;
        for(std::size_t i=0;i<N;++i)if(hover_[i].Active(now)||press_[i].Active(now)||selected_[i].Active(now))return true;
        return false;
    }
};
}
