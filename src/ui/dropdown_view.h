#pragma once
#include "ui/controls.h"
#include <algorithm>
namespace lumashot::ui {
struct Dropdown {
    int property{-1},selected{-1},hover{-1};Box bounds{};float scale{1};
    std::vector<std::pair<std::wstring,int>> items;
    bool Open()const{return property>=0;}
    Box Row(int index)const{return {bounds.left+4*scale,bounds.top+(4+index*30)*scale,bounds.right-4*scale,bounds.top+(34+index*30)*scale};}
    int Hit(Point p)const{if(Open())for(int i=0;i<int(items.size());++i)if(Contains(Row(i),p))return i;return -1;}
    void Close(){property=-1;items.clear();hover=selected=-1;}
    void Place(Box anchor,RECT monitor,float dpi,float minimum=156){
        scale=std::min(dpi,float(monitor.bottom-monitor.top-16)/(8+30*float(items.size())));
        const float height=(8+30*float(items.size()))*scale;
        const float width=std::min(std::max(anchor.right-anchor.left,minimum*scale),float(monitor.right-monitor.left)-16);
        const float x=std::clamp(anchor.left,float(monitor.left)+8,std::max(float(monitor.left)+8,float(monitor.right)-8-width));
        float y=anchor.bottom+4*scale;if(y+height>monitor.bottom-8)y=anchor.top-height-4*scale;
        y=std::clamp(y,float(monitor.top)+8,std::max(float(monitor.top)+8,float(monitor.bottom)-8-height));bounds={x,y,x+width,y+height};
    }
};
// Shared row chrome; callers may draw a sample between the check and label.
inline float DrawDropdownRow(const PaintContext& p,const Theme& theme,Box row,bool hover,bool selected){
    const float s=theme.scale;
    if(hover||selected)p.target->FillRoundedRectangle(D2D1::RoundedRect({row.left,row.top,row.right,row.bottom},5*s,5*s),p.brush(hover?0x55488fe0:0x3364acff));
    if(selected){const float x=row.left+11*s,y=(row.top+row.bottom)/2;p.target->DrawLine({x-4*s,y},{x-s,y+3*s},p.brush(theme.Accent()),1.6f*s);p.target->DrawLine({x-s,y+3*s},{x+5*s,y-4*s},p.brush(theme.Accent()),1.6f*s);}
    return row.left+28*s;
}
}
