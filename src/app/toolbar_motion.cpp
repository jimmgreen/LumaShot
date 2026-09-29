#include "app/application.h"
#include <algorithm>
#include <cmath>

namespace lumashot {
void Application::StopToolbarMotion(){
    if(main_)KillTimer(main_,22);
    toolbar_motion_timer_=false;toolbar_pressed_=-1;toolbar_motion_.Reset();state_.toolbar_visual={};
}
void Application::SyncToolbarMotion(uint64_t now){
    if(!active_||!state_.selected||state_.busy){StopToolbarMotion();return;}
    const auto controls=ToolbarControls(state_,document_);std::array<Box,ui::ToolbarSlots> boxes{};
    const auto origin=state_.toolbar.bounds;
    for(const auto& c:controls)if(c.id>=0&&c.id<int(ui::ToolbarSlots)&&c.Interactive())
        boxes[size_t(c.id)]={c.bounds.left-origin.left,c.bounds.top-origin.top,c.bounds.right-origin.left,c.bounds.bottom-origin.top};
    const int selected=state_.tool==Tool::Number?14:static_cast<int>(state_.tool);
    const bool obscured=state_.picker.open||state_.dropdown.Open();
    toolbar_motion_.Update(boxes,selected,obscured?-1:state_.hover,obscured?-1:toolbar_pressed_,now,toolbar_effects_);
    if(toolbar_motion_.Active(now)){
        if(!toolbar_motion_timer_){
            toolbar_motion_timer_=main_&&SetTimer(main_,22,16,nullptr)!=0;
            if(!toolbar_motion_timer_)toolbar_motion_.Update(boxes,selected,state_.hover,toolbar_pressed_,now,false);
        }
    }else{if(main_)KillTimer(main_,22);toolbar_motion_timer_=false;}
    state_.toolbar_visual=toolbar_motion_.Sample(now);
    auto& b=state_.toolbar_visual.indicator;
    b.left+=origin.left;b.right+=origin.left;b.top+=origin.top;b.bottom+=origin.top;
}
void Application::TickToolbarMotion(uint64_t now){
    SyncToolbarMotion(now);
    if(!state_.toolbar_visual.ready)return;
    // Do not wake overlays on unrelated monitors. The existing renderer retains
    // ownership of frozen desktop resources; no capture occurs during a tick.
    const auto b=state_.toolbar.bounds;const float pad=4*state_.toolbar.scale;
    const RECT dirty{LONG(std::floor(b.left-pad)),LONG(std::floor(b.top-pad)),LONG(std::ceil(b.right+pad)),LONG(std::ceil(b.bottom+pad))};
    for(auto& view:views_){RECT clipped{};if(IntersectRect(&clipped,&dirty,&view->bounds)){
        OffsetRect(&clipped,-view->bounds.left,-view->bounds.top);InvalidateRect(view->window,&clipped,FALSE);
    }}
}
}
