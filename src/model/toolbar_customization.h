#pragma once
#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace lumashot {
// Capture-toolbar entries the user may move between the panel and the "更多" menu.
// 1-6 and 14 are annotation tools (rectangle ... mosaic, number); 13 pin (置顶贴图), 15 OCR,
// 18 screenshot translation, 17 long capture, 16 recording. Select (0) and the fixed actions
// (undo, redo, save, copy, cancel, done) always stay on the panel.
inline constexpr std::array<int,12> CustomizableToolbarIds{1,2,3,4,5,6,14,13,15,18,17,16};
inline constexpr int ToolbarPinId=13;
inline constexpr int ToolbarMoreId=19;
// Dropdown property of the right-click menu on a customizable panel button.
inline constexpr int ToolbarContextMenuId=90;
inline constexpr int ToolbarRestoreAllItem=-2;
// "属性" button: shown in the tool grid while 自动展开属性栏 is off and a tool is active.
// Must not collide with property control ids (20-30 colours, 40-86 fields): 23 used to be the
// fourth colour swatch, so clicking green toggled the card instead of picking the colour.
inline constexpr int ToolbarPropertiesId=88;
constexpr bool IsToolbarToolId(int id){return (id>=1&&id<=6)||id==14;}
constexpr bool IsCustomizableToolbarId(int id){return std::find(CustomizableToolbarIds.begin(),CustomizableToolbarIds.end(),id)!=CustomizableToolbarIds.end();}
enum class ToolbarLabels{Auto,Text,Icons};
// 属性栏样式: Strip = one row with each caption left of its controls (default); Card = captions above
// the controls, a roomier two-band card.
enum class PropertyBarStyle{Strip,Card};
// Surface behind the screenshot toolbar and its property card.
enum class ToolbarSurfaceStyle{Acrylic,Solid};

struct ToolbarCustomization {
    std::vector<int> order{CustomizableToolbarIds.begin(),CustomizableToolbarIds.end()};
    // Entries shown in the "更多" menu, kept in panel order. Empty (the default) keeps
    // every entry on the panel, so existing users see no change.
    std::vector<int> overflow;
    ToolbarLabels labels{ToolbarLabels::Auto};
    bool auto_properties{true};
    PropertyBarStyle property_style{PropertyBarStyle::Strip};
    ToolbarSurfaceStyle surface_style{ToolbarSurfaceStyle::Acrylic};
    bool operator==(const ToolbarCustomization&)const=default;
    bool OnPanel(int id)const{return !IsCustomizableToolbarId(id)||std::find(overflow.begin(),overflow.end(),id)==overflow.end();}
    void SetOnPanel(int id,bool on){
        if(!IsCustomizableToolbarId(id))return;
        std::erase(overflow,id);if(!on)overflow.push_back(id);
        Normalize();
    }
    // Drops unknown and duplicate ids, appends missing entries in default order and
    // keeps the overflow list in panel order. The pin became customizable after the quick
    // actions: a saved order without it keeps the pin in its old slot before them.
    void Normalize(){
        std::vector<int> clean;
        for(int id:order)if(IsCustomizableToolbarId(id)&&std::find(clean.begin(),clean.end(),id)==clean.end())clean.push_back(id);
        for(int id:CustomizableToolbarIds){
            if(std::find(clean.begin(),clean.end(),id)!=clean.end())continue;
            const auto slot=id==ToolbarPinId?std::find_if(clean.begin(),clean.end(),[](int entry){return !IsToolbarToolId(entry);}):clean.end();
            clean.insert(slot,id);
        }
        order=std::move(clean);
        std::vector<int> hidden;
        for(int id:order)if(std::find(overflow.begin(),overflow.end(),id)!=overflow.end())hidden.push_back(id);
        overflow=std::move(hidden);
        if(static_cast<int>(labels)<0||static_cast<int>(labels)>2)labels=ToolbarLabels::Auto;
        if(static_cast<int>(property_style)<0||static_cast<int>(property_style)>1)property_style=PropertyBarStyle::Strip;
        if(static_cast<int>(surface_style)<0||static_cast<int>(surface_style)>1)surface_style=ToolbarSurfaceStyle::Acrylic;
    }
};
inline std::wstring FormatToolbarIds(const std::vector<int>& ids){
    std::wstring result;
    for(int id:ids){if(!result.empty())result+=L',';result+=std::to_wstring(id);}
    return result;
}
inline std::vector<int> ParseToolbarIds(std::wstring_view text){
    std::vector<int> result;int value=0;bool digits=false;
    for(wchar_t ch:text){
        if(ch>=L'0'&&ch<=L'9'){value=std::min(999,value*10+static_cast<int>(ch-L'0'));digits=true;}
        else{if(digits)result.push_back(value);value=0;digits=false;}
    }
    if(digits)result.push_back(value);
    return result;
}
}
