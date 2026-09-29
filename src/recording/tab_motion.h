#pragma once
#include "ui/toolbar_motion.h"
namespace lumashot::recording {
// Stable coordinates relative to the three-slot group, not the resizing panel.
inline int TabSlot(int id){return id==40?1:id==1?2:id==2?3:-1;}
inline std::array<Box,ui::ToolbarSlots> TabSlots(){
    std::array<Box,ui::ToolbarSlots> boxes{};
    for(int i=1;i<=3;++i)boxes[size_t(i)]={float((i-1)*82),10,float(i*82),36};
    return boxes;
}
}
