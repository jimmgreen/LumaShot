#pragma once
#include "model/document.h"

namespace lumashot {
struct MosaicTile { RECT bounds; uint32_t color; };
std::vector<MosaicTile> BuildMosaicTiles(const Frame& frame, const Mark& mark);
Frame BuildMosaicBlur(const Frame& frame,const Mark& mark);
}
