#pragma once
// Frozen pre-optimization oracle. Keep independent of production coverage/cache
// helpers so boundary, brush transparency and rounding regressions stay visible.
#include "model/mosaic.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace lumashot::test_reference {
std::vector<MosaicTile> Tiles(const Frame&,const Mark&);

Frame Blur(const Frame& frame,const Mark& mark){
    RECT region=PixelRect(LocalBounds(mark)),clipped{};
    if(!IntersectRect(&clipped,&region,&frame.bounds))return {};
    const int radius=std::clamp(int(std::lround(mark.mosaic_cell)),2,128);
    RECT expanded{std::max(frame.bounds.left,clipped.left-radius),std::max(frame.bounds.top,clipped.top-radius),std::min(frame.bounds.right,clipped.right+radius),std::min(frame.bounds.bottom,clipped.bottom+radius)};
    Frame source=Crop(frame,expanded),temporary=MakeFrame(expanded);
    const int width=source.Width(),height=source.Height();
    // Separable sliding box filter: work is linear in the affected area.
    for(int pass=0;pass<2;++pass){
        const int lines=pass==0?height:width,length=pass==0?width:height;
        const auto index=[&](int line,int position){return pass==0?static_cast<size_t>(line)*width+position:static_cast<size_t>(position)*width+line;};
        for(int line=0;line<lines;++line){
            int r{},g{},b{},count{};
            const auto add=[&](int position,int sign){const auto c=source.pixels[index(line,position)];r+=sign*int((c>>16)&255);g+=sign*int((c>>8)&255);b+=sign*int(c&255);count+=sign;};
            for(int p=0;p<=std::min(radius,length-1);++p)add(p,1);
            for(int p=0;p<length;++p){
                temporary.pixels[index(line,p)]=0xff000000|(uint32_t(r/count)<<16)|(uint32_t(g/count)<<8)|uint32_t(b/count);
                if(p-radius>=0)add(p-radius,-1);if(p+radius+1<length)add(p+radius+1,1);
            }
        }
        source.pixels.swap(temporary.pixels);
    }
    Frame output=MakeFrame(clipped,0);
    // Reuse the brush coverage calculation on a fine grid; rectangle masks
    // remain exact, and pixels outside the mask stay fully transparent.
    Mark mask=mark;mask.mosaic_cell=4;
    for(const auto& tile:Tiles(frame,mask)){
        RECT covered{};if(!IntersectRect(&covered,&tile.bounds,&clipped))continue;
        for(LONG y=covered.top;y<covered.bottom;++y)for(LONG x=covered.left;x<covered.right;++x)
            output.pixels[static_cast<size_t>(y-clipped.top)*output.Width()+x-clipped.left]=source.pixels[static_cast<size_t>(y-expanded.top)*width+x-expanded.left];
    }
    return output;
}

std::vector<MosaicTile> Tiles(const Frame& frame, const Mark& mark) {
    const int cell = std::clamp(static_cast<int>(std::lround(mark.mosaic_cell)), 4, 128);
    const int columns = (frame.Width() + cell - 1) / cell;
    const int rows = (frame.Height() + cell - 1) / cell;
    std::unordered_set<int> touched;
    RECT region = PixelRect(LocalBounds(mark)), clipped{};
    if (!IntersectRect(&clipped, &region, &frame.bounds)) return {};
    if (mark.points.empty()) {
        for (int y = (clipped.top - frame.bounds.top) / cell; y <= (clipped.bottom - 1 - frame.bounds.top) / cell; ++y)
            for (int x = (clipped.left - frame.bounds.left) / cell; x <= (clipped.right - 1 - frame.bounds.left) / cell; ++x)
                touched.insert(y * columns + x);
    } else {
        const float radius = std::max(4.0f, mark.mosaic_brush / 2);
        const auto stamp = [&](Point point) {
            const float px = point.x - frame.bounds.left, py = point.y - frame.bounds.top;
            const int x0 = std::max(0, int(std::floor((px - radius) / cell)));
            const int y0 = std::max(0, int(std::floor((py - radius) / cell)));
            const int x1 = std::min(columns - 1, int(std::floor((px + radius) / cell)));
            const int y1 = std::min(rows - 1, int(std::floor((py + radius) / cell)));
            for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
                const float nearest_x = std::clamp(px, float(x * cell), float((x + 1) * cell));
                const float nearest_y = std::clamp(py, float(y * cell), float((y + 1) * cell));
                if (std::hypot(px - nearest_x, py - nearest_y) <= radius) touched.insert(y * columns + x);
            }
        };
        stamp(mark.points.front());
        for (size_t i = 1; i < mark.points.size(); ++i) {
            const Point a = mark.points[i - 1], b = mark.points[i];
            const int steps = std::max(1, int(std::ceil(std::hypot(b.x - a.x, b.y - a.y) / std::max(1.0f, radius / 2))));
            for (int step = 1; step <= steps; ++step) {
                const float t = float(step) / steps;
                stamp({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t});
            }
        }
    }
    std::vector<int> indices(touched.begin(), touched.end());
    std::sort(indices.begin(), indices.end());
    std::vector<MosaicTile> tiles;
    tiles.reserve(indices.size());
    for (const int index : indices) {
        const int x = frame.bounds.left + (index % columns) * cell;
        const int y = frame.bounds.top + (index / columns) * cell;
        RECT tile{x, y, std::min(x + cell, int(frame.bounds.right)), std::min(y + cell, int(frame.bounds.bottom))};
        if (mark.points.empty()) { RECT trimmed{}; if (!IntersectRect(&trimmed, &tile, &clipped)) continue; tile = trimmed; }
        uint64_t r{}, g{}, b{}, count{};
        for (int py = tile.top; py < tile.bottom; ++py) for (int px = tile.left; px < tile.right; ++px) {
            const uint32_t pixel = frame.pixels[static_cast<size_t>(py - frame.bounds.top) * frame.Width() + px - frame.bounds.left];
            r += (pixel >> 16) & 255; g += (pixel >> 8) & 255; b += pixel & 255; ++count;
        }
        if (count) tiles.push_back({tile, 0xff000000 | (uint32_t(r / count) << 16) | (uint32_t(g / count) << 8) | uint32_t(b / count)});
    }
    return tiles;
}
}
