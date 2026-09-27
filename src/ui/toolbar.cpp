#include "ui/render.h"
#include "model/arrow.h"
#include "model/note_color.h"
#include "ui/fonts.h"
#include <algorithm>
#include <cmath>

namespace lumashot {
namespace {
struct PropertyField { int id;ui::Kind kind;float width;LPCWSTR text=L"";int icon=-1; };
struct PropertyGroup { LPCWSTR title;std::vector<PropertyField> fields; };
std::vector<PropertyGroup> PropertyGroups(Tool tool,NumberCombo combo) {
    using K=ui::Kind;
    PropertyGroup colors{tool==Tool::Text?L"文字 / 背景色系":L"颜色",{}};
    for(int id:AnnotationColorIds)colors.fields.push_back({id,K::Swatch,22});
    colors.fields.push_back({28,K::Swatch,22});
    const PropertyGroup width{L"线宽",{{27,K::Dropdown,76}}};
    const PropertyGroup line{L"线型",{{48,K::Dropdown,80}}};
    PropertyGroup fill{L"填充",{{40,K::Button,62,L"无填充",-2}}};
    for(int id:AnnotationFillIds)fill.fields.push_back({id,K::Swatch,20});
    switch(tool){
    case Tool::Rectangle:return {colors,width,line,fill,{L"填充透明度",{{46,K::Slider,136}}},{L"圆角",{{47,K::Slider,128}}}};
    case Tool::Ellipse:return {colors,width,fill,{L"填充透明度",{{46,K::Slider,136}}}};
    case Tool::Arrow:return {colors,width,line,{L"箭头类型",{{49,K::Dropdown,106}}},{L"箭头头部",{{66,K::Dropdown,106}}},{L"箭头大小",{{50,K::Dropdown,80}}}};
    case Tool::Pen:return {{L"轨迹",{{83,K::Button,76,L"自由画笔"},{84,K::Button,48,L"直线"},{85,K::Button,76,L"连续直线"}}},{L"辅助",{{86,K::Button,64,L"捕捉 开"}}},{L"绘制模式",{{58,K::Button,48,L"普通"},{59,K::Button,62,L"荧光笔"}}},colors,{L"线宽",{{27,K::Dropdown,64}}},{L"透明度",{{56,K::Slider,112}}},{L"平滑度",{{57,K::Dropdown,80}}}};
    case Tool::Text:return {colors,{L"字体",{{80,K::Dropdown,150}}},{L"字号 / 加粗",{{27,K::Dropdown,80},{51,K::Button,30,L"",51}}},{L"背景",{{52,K::Dropdown,106}}},{L"对齐",{{53,K::Button,28,L"",53},{54,K::Button,28,L"",54},{55,K::Button,28,L"",55}}}};
    case Tool::Mosaic:return {{L"模式",{{60,K::Button,62,L"像素"},{61,K::Button,62,L"模糊"}}},{L"方式",{{62,K::Button,62,L"涂抹"},{63,K::Button,62,L"框选"}}},{L"笔刷大小",{{64,K::Slider,140}}},{L"强度",{{65,K::Slider,140}}}};
    case Tool::Number:{
        std::vector<PropertyGroup> result{colors,{L"外形",{{67,K::Dropdown,90}}},{L"大小",{{68,K::Dropdown,68}}},{L"组合形式",{{69,K::Dropdown,92}}},{L"自定义标签",{{81,K::TextField,140}}}};
        if(combo==NumberCombo::Text){result.push_back({L"文字颜色",{{79,K::Button,40,L"自动"},{72,K::Swatch,24},{73,K::Swatch,24},{74,K::Swatch,24},{75,K::Swatch,24},{76,K::Swatch,24},{77,K::Swatch,24},{70,K::Swatch,24}}});result.push_back({L"字号",{{71,K::Dropdown,64}}});}
        return result;
    }
    default:return {};
    }
}
// A single geometry source for drawing, hit testing, sliders and popup anchors.
std::vector<ui::Control> PropertyControls(Tool tool,NumberCombo combo,Box area,float scale,float* height=nullptr) {
    const auto groups=PropertyGroups(tool,combo);std::vector<ui::Control> result;
    const float available=(area.right-area.left)/scale,gap=14,fieldGap=2,rowHeight=62;
    std::vector<float> widths;for(const auto& g:groups){float w=0;for(const auto& f:g.fields)w+=f.width+fieldGap;widths.push_back(w-fieldGap);}
    float y=0;
    for(size_t first=0;first<groups.size();){
        size_t end=first;float used=0;
        while(end<groups.size()&&(end==first||used+gap+widths[end]<=available)){used+=(end==first?0:gap)+widths[end];++end;}
        const float extra=std::max(0.f,available-used)/float(end-first);float x=0;
        for(size_t i=first;i<end;++i){
            const auto& g=groups[i];const float w=std::min(available,widths[i]+extra);
            const auto box=[&](float l,float t,float r,float b){return Box{area.left+l*scale,area.top+t*scale,std::min(area.right,area.left+r*scale),area.top+b*scale};};
            if(i>first){ui::Control separator;separator.kind=ui::Kind::Separator;separator.bounds=box(x-gap/2,y+2,x-gap/2,y+48);result.push_back(separator);}
            ui::Control label;label.kind=ui::Kind::Label;label.text=g.title;label.bounds=box(x,y,x+w,y+18);result.push_back(label);
            const float fit=std::min(1.f,available/widths[i]);float offset=0;const float stretch=g.fields.size()==1?w-widths[i]:0;
            for(const auto& f:g.fields){
                ui::Control c;c.id=f.id;c.kind=f.kind;c.text=f.text;c.icon=f.icon;c.square=f.kind==ui::Kind::Swatch;c.rainbow=f.id==28||f.id==70;
                c.bounds=box(x+offset,y+22,x+offset+(f.width+stretch)*fit,y+50);result.push_back(c);offset+=(f.width+stretch+fieldGap)*fit;
            }
            x+=w+gap;
        }
        first=end;y+=rowHeight;
    }
    if(height)*height=groups.empty()?0:y-12;
    return result;
}
}
Box ToolbarLayout::Button(int id)const {
    const float s=scale;
    if((id>=1&&id<=6)||id==14){const int index=id==14?6:id-1;const float w=(tools.right-tools.left)/columns;const float x=tools.left+(index%columns)*w,y=tools.top+(index/columns)*36*s;return {x,y,x+w-4*s,y+32*s};}
    if((id==15&&!ocr_available)||(id==16&&!recording_available))return {};
    constexpr std::array<int,10> order{0,7,8,9,10,13,15,16,11,12};
    const auto slots=[&](int action){return action==15?(ocr_available?2:0):action==16?(recording_available?1:0):1;};
    int total=0,slot=0;bool found=false;
    for(int action:order){total+=slots(action);if(action==id)found=true;else if(!found)slot+=slots(action);}
    if(!found)return {};
    const float pitch=(actions.right-actions.left)/total;
    const float x=actions.left+slot*pitch,y=actions.top;return {x+1*s,y+2*s,x+slots(id)*pitch-s,y+28*s};
}
Box ToolbarLayout::Property(int id,Tool tool)const {
    if(tool==Tool::Select)tool=property_tool;
    for(const auto& c:PropertyControls(tool,number_combo,colors,scale))if(c.id==id)return c.bounds;
    return {};
}
ToolbarLayout PlaceToolbar(Box selection,RECT monitor,float scale,float expansion,Tool tool,NumberCombo combo) {
    // Keep action labels and hit targets usable on narrow, high-DPI displays.
    scale=std::min(scale,float(monitor.right-monitor.left)/304.f);
    ToolbarLayout t;t.scale=scale;t.property_tool=tool;t.number_combo=combo;const float s=scale;
    const float width=std::min(944.f,float(monitor.right-monitor.left)/s-16);const bool inlineActions=width>=844;
    const int columns=std::clamp(int((width-24-(inlineActions?316:0))/82),1,7);t.columns=columns;
    const float toolHeight=36.f*((7+columns-1)/columns);
    t.tools={12*s,11*s,(width-12-(inlineActions?316:0))*s,(11+toolHeight)*s};
    const float bodyTop=18+toolHeight+(inlineActions?0:36);
    t.colors={12*s,(bodyTop+6)*s,(width-12)*s,0};float propertyHeight=0;
    PropertyControls(tool,combo,t.colors,s,&propertyHeight);t.colors.bottom=t.colors.top+propertyHeight*s;
    const float fullHeight=bodyTop+6+propertyHeight+12;
    if(fullHeight*s>float(monitor.bottom-monitor.top)-16)return PlaceToolbar(selection,monitor,(float(monitor.bottom-monitor.top)-16)/fullHeight,expansion,tool,combo);
    const float height=bodyTop+(fullHeight-bodyTop)*std::clamp(expansion,0.f,1.f);
    const float actionTop=inlineActions?12:14+toolHeight;const float actionLeft=std::max(12.f,width-320);
    t.actions={actionLeft*s,actionTop*s,(width-12)*s,(actionTop+30)*s};
    const float left=std::clamp(selection.right-width*s,float(monitor.left)+8*s,std::max(float(monitor.left)+8*s,float(monitor.right)-(width+8)*s));
    float top=selection.bottom+10*s;
    if(top+fullHeight*s>monitor.bottom-8*s&&selection.top-(fullHeight+10)*s>=monitor.top+8*s)top=selection.top-(height+10)*s;
    top=std::clamp(top,float(monitor.top)+8*s,std::max(float(monitor.top)+8*s,float(monitor.bottom)-(height+8)*s));
    const auto move=[&](Box& b){b.left+=left;b.right+=left;b.top+=top;b.bottom+=top;};move(t.tools);move(t.actions);move(t.colors);
    t.bounds={left,top,left+width*s,top+height*s};return t;
}
std::vector<ui::Control> ToolbarLayout::Controls(Tool tool)const {
    using ui::Kind;std::vector<ui::Control> result;
    ui::Control surface;surface.kind=Kind::Surface;surface.bounds=bounds;result.push_back(surface);
    constexpr std::array<LPCWSTR,7> names{L"",L"矩形",L"圆形",L"箭头",L"铅笔",L"文字",L"马赛克"};
    for(int i=0;i<17;++i){if((i==15&&!ocr_available)||(i==16&&!recording_available))continue;ui::Control c;c.id=i;c.kind=Kind::Button;c.bounds=Button(i);c.text=i==15?L"OCR":i==14?L"序号":(i<=6?names[i]:L"");c.icon=i==15?-1:i;result.push_back(c);}
    if(tool!=Tool::Select){auto properties=PropertyControls(tool,number_combo,colors,scale);result.insert(result.end(),properties.begin(),properties.end());}
    return result;
}
int ToolbarLayout::Hit(Point p,Tool tool)const{return ui::HitTest(Controls(tool),p);}
std::optional<uint32_t> ToolbarColor(int id) {
    for(size_t i=0;i<AnnotationColors.size();++i){
        if(id==AnnotationColorIds[i]||id==AnnotationFillIds[i]||id==72+int(i))return AnnotationColors[i];
    }
    return std::nullopt;
}
std::optional<uint32_t> PenToolbarColor(int id,const ViewState&) {
    return ToolbarColor(id);
}
std::vector<ui::Control> ToolbarControls(const ViewState& state,const Document& document) {
    auto controls=state.toolbar.Controls(state.PropertyTool()==Tool::Select?state.closing_tool:state.PropertyTool());
    for(auto& c:controls) {
        c.enabled=c.enabled&&!state.busy;
        if(c.id==16)c.enabled=c.enabled&&state.toolbar.recording_available&&state.selected&&state.selection.right-state.selection.left>=8&&state.selection.bottom-state.selection.top>=8;
        if(c.id>=20)c.enabled=c.enabled&&state.PropertyTool()!=Tool::Select&&c.bounds.bottom<=state.toolbar.bounds.bottom-8*state.toolbar.scale;
        if(c.id>=0&&c.id<=6)c.selected=c.id==static_cast<int>(state.tool);
        if(c.id==14)c.selected=state.tool==Tool::Number;
        if(c.id==67){c.text=state.number_label.empty()?NumberShapeNames[static_cast<int>(state.number_shape)]:L"自动圆角";c.enabled=c.enabled&&state.number_label.empty();}
        if(c.id==81)c.text=state.number_label.empty()?L"自动序号（留空）":state.number_label;
        if(c.id==68)c.text=std::to_wstring(int(state.number_size))+L" px";
        if(c.id==69)c.text=NumberComboNames[static_cast<int>(state.number_combo)];
        if(c.id>=72&&c.id<=77)c.selected=state.number_text_preset==-1&&ToolbarColor(c.id)==state.number_text_color;
        if(c.id==79)c.selected=state.number_text_preset==-2;
        if(c.id==71)c.text=std::to_wstring(int(state.number_text_size));
        if(c.id==7)c.enabled=c.enabled&&(state.polyline_active||document.CanUndo());if(c.id==8)c.enabled=c.enabled&&!state.polyline_active&&document.CanRedo();c.primary=c.id==12;
        if(auto color=PenToolbarColor(c.id,state))c.color=*color;
        if(c.kind==ui::Kind::Swatch&&c.id<40)c.selected=c.color==state.ActiveColor();
        if(c.id==24)c.text=std::to_wstring(int(state.mosaic_brush))+L" px";
        if(c.id==27)c.text=std::to_wstring(int(state.PropertyTool()==Tool::Text?state.font_size:(state.PropertyTool()==Tool::Mosaic?state.mosaic_cell:state.ActiveWidth())))+L" px";
        if(c.id==40)c.selected=!state.fill_color;
        if((c.id>=41&&c.id<=45)||c.id==82){c.square=true;c.selected=state.fill_color==c.color;}
        if(c.id==46){c.value=1-state.fill_opacity;c.text=std::to_wstring(int(std::round(c.value*100)))+L"%";c.enabled=c.enabled&&state.fill_color.has_value();}
        if(c.id==47){c.value=state.corner_radius/32;c.text=std::to_wstring(int(state.corner_radius))+L" px";}
        if(c.id==48){constexpr LPCWSTR names[]={L"实线",L"虚线",L"点线",L"点划线"};c.text=names[static_cast<int>(state.line_style)];}
        if(c.id==49)c.text=ArrowTypeNames[static_cast<int>(state.arrow_type)];
        if(c.id==66)c.text=ArrowHeadNames[static_cast<int>(state.arrow_head)];
        if(c.id==50)c.text=std::to_wstring(int(state.arrow_size))+L" px";
        if(c.id==51)c.selected=state.text_bold;
        if(c.id==80)c.text=TextFontName(state.font_family);
        if(c.id==52){constexpr LPCWSTR names[]={L"无背景",L"旧版白底",L"旧版深底",L"旧版黄底",L"同色浅底",L"同色深底",L"旧版自动",L"半透明自动",L"半透明浅色",L"半透明深色"};c.text=names[static_cast<int>(state.text_background)];}
        if(c.id>=53&&c.id<=55)c.selected=static_cast<int>(state.text_align)==c.id-53;
        if(c.id==56){c.value=1-state.PenOpacity();c.text=std::to_wstring(int(std::round(c.value*100)))+L"%";}
        if(c.id==83)c.selected=!state.pen_straight&&!state.pen_polyline;
        if(c.id==84)c.selected=state.pen_straight&&!state.pen_polyline;
        if(c.id==85)c.selected=state.pen_polyline;
        if(c.id==86){c.selected=state.pen_snap;c.text=state.pen_snap?L"捕捉 开":L"捕捉 关";c.enabled=c.enabled&&state.pen_straight;}
        if(c.id==58||c.id==59)c.selected=(c.id==59)==(state.pen_mode==PenMode::Highlighter);
        if(c.id==57){constexpr LPCWSTR names[]={L"无平滑",L"标准",L"高平滑"};c.text=names[static_cast<int>(state.pen_smoothing)];c.enabled=c.enabled&&!state.pen_straight;}
        if(c.id==60||c.id==61)c.selected=static_cast<int>(state.mosaic_mode)==c.id-60;
        if(c.id==62||c.id==63)c.selected=static_cast<int>(state.mosaic_method)==c.id-62;
        if(c.id==64){c.value=(state.mosaic_brush-4)/124;c.text=std::to_wstring(int(state.mosaic_brush))+L" px";c.enabled=c.enabled&&state.mosaic_method==MosaicMethod::Brush;}
        if(c.id==65){c.value=(state.mosaic_strength-1)/99;c.text=std::to_wstring(int(state.mosaic_strength))+L"%";}
    }
    return controls;
}
bool SetToolbarSlider(ViewState& state,int id,Point point) {
    if(state.busy||!state.selected)return false;
    if(state.PropertyTool()==Tool::Mosaic&&id==64&&state.mosaic_method==MosaicMethod::Brush){state.mosaic_brush=std::round(4+124*ui::SliderValue(state.toolbar.Property(id),point,state.toolbar.scale));return true;}
    if(state.PropertyTool()==Tool::Mosaic&&id==65){state.mosaic_strength=std::round(1+99*ui::SliderValue(state.toolbar.Property(id),point,state.toolbar.scale));return true;}
    if(id==56&&state.PropertyTool()==Tool::Pen){state.PenOpacity()=1-ui::SliderValue(state.toolbar.Property(id,state.PropertyTool()),point,state.toolbar.scale);return true;}
    if(id==46&&(state.PropertyTool()==Tool::Rectangle||state.PropertyTool()==Tool::Ellipse)&&state.fill_color){state.fill_opacity=1-ui::SliderValue(state.toolbar.Property(id),point,state.toolbar.scale);return true;}
    if(id==47&&state.PropertyTool()==Tool::Rectangle){state.corner_radius=std::round(ui::SliderValue(state.toolbar.Property(id),point,state.toolbar.scale)*32);return true;}
    return false;
}
bool AdjustPropertyValue(ViewState& state,int id,int steps) {
    if(state.busy||!state.selected||state.PropertyTool()==Tool::Select||steps==0)return false;
    if(state.PropertyTool()==Tool::Number&&state.number_combo==NumberCombo::Text&&id==71){state.number_text_size=std::clamp(state.number_text_size+float(steps),8.f,64.f);return true;}
    if(state.PropertyTool()==Tool::Number&&id==68){state.number_size=std::clamp(state.number_size+float(steps),16.f,96.f);return true;}
    if(state.PropertyTool()==Tool::Mosaic&&id==64&&state.mosaic_method==MosaicMethod::Brush){state.mosaic_brush=std::clamp(state.mosaic_brush+float(steps),4.f,128.f);return true;}
    if(state.PropertyTool()==Tool::Mosaic&&id==65){state.mosaic_strength=std::clamp(state.mosaic_strength+float(steps),1.f,100.f);return true;}
    if(id==56&&state.PropertyTool()==Tool::Pen){state.PenOpacity()=std::clamp(state.PenOpacity()-float(steps)*.01f,0.f,1.f);return true;}
    if(id==50&&state.PropertyTool()==Tool::Arrow){state.arrow_size=std::clamp(state.arrow_size+float(steps),4.f,64.f);return true;}
    if(id==46&&(state.PropertyTool()==Tool::Rectangle||state.PropertyTool()==Tool::Ellipse)&&state.fill_color){state.fill_opacity=std::clamp(state.fill_opacity-float(steps)*.01f,0.f,1.f);return true;}
    if(id==47&&state.PropertyTool()==Tool::Rectangle){state.corner_radius=std::clamp(state.corner_radius+float(steps),0.f,32.f);return true;}
    if(id<24||id>27)return false;
    float* value=&state.ActiveWidth();float limit=state.Highlighting()?64.f:16.f;
    if(state.PropertyTool()==Tool::Text){value=&state.font_size;limit=96;}
    if(state.PropertyTool()==Tool::Mosaic){value=id==27?&state.mosaic_cell:&state.mosaic_brush;limit=id==27?64.f:128.f;}
    const float minimum=state.PropertyTool()==Tool::Text?8.f:(state.PropertyTool()==Tool::Mosaic?4.f:1.f);
    *value=std::clamp(*value+float(steps),minimum,limit);return true;
}
}


