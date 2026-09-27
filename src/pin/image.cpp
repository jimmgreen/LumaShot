#include "pin/image.h"
#include <algorithm>
namespace lumashot {
Frame PinOcrImage(const Frame& frozen,const Frame* patch,const Document& document,RECT selection) {
    Frame raw=Crop(frozen,selection);
    if(patch&&!patch->pixels.empty()) {
        RECT overlap{};
        if(IntersectRect(&overlap,&raw.bounds,&patch->bounds))for(LONG y=overlap.top;y<overlap.bottom;++y)
            std::copy_n(patch->pixels.data()+static_cast<size_t>(y-patch->bounds.top)*patch->Width()+overlap.left-patch->bounds.left,
                overlap.right-overlap.left,raw.pixels.data()+static_cast<size_t>(y-raw.bounds.top)*raw.Width()+overlap.left-raw.bounds.left);
    }
    Renderer renderer;return renderer.Flatten(raw,document,selection);
}
}
