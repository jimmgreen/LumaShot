#include "ui/render.h"
#include "model/arrow.h"
#include "ui/fonts.h"
#include <algorithm>

namespace lumashot {
bool OpenToolbarDropdown(ViewState& state,int id,RECT monitor) {
    if(state.busy||!state.selected)return false;
    ViewState::Dropdown popup;popup.property=id;
    int current{};
    const auto numeric=[&](std::initializer_list<int> values){for(int value:values)popup.items.emplace_back(std::to_wstring(value)+L" px",value);};
    if(id==80&&state.PropertyTool()==Tool::Text){const auto& fonts=TextFonts();for(int i=0;i<static_cast<int>(fonts.size());++i){popup.items.emplace_back(fonts[i].name,i);if(fonts[i].family==state.font_family)current=i;}
    }else if(id==71&&state.PropertyTool()==Tool::Number&&state.number_combo==NumberCombo::Text){numeric({8,12,14,16,18,20,24,28,32,40,48,64});current=int(state.number_text_size);
    }else if(id==69&&state.PropertyTool()==Tool::Number){for(int i=0;i<7;++i)popup.items.emplace_back(NumberComboNames[i],i);current=static_cast<int>(state.number_combo);
    }else if(id==67&&state.PropertyTool()==Tool::Number){for(int i=0;i<10;++i)popup.items.emplace_back(NumberShapeNames[i],i);current=static_cast<int>(state.number_shape);
    }else if(id==68&&state.PropertyTool()==Tool::Number){numeric({16,20,24,28,32,40,48,64,96});current=int(state.number_size);
    }else if(id==66&&state.PropertyTool()==Tool::Arrow){
        for(int i=0;i<11;++i)popup.items.emplace_back(ArrowHeadNames[i],i);current=static_cast<int>(state.arrow_head);
    }else if(id==57&&state.PropertyTool()==Tool::Pen){
        popup.items={{L"无平滑",0},{L"标准",1},{L"高平滑",2}};current=static_cast<int>(state.pen_smoothing);
    }else if(id==52&&state.PropertyTool()==Tool::Text){
        popup.items={{L"无背景",0},{L"半透明 · 自动",7},{L"半透明 · 浅色",8},{L"半透明 · 深色",9},{L"增强 · 浅实底",4},{L"增强 · 深实底",5}};current=static_cast<int>(state.text_background);
    }else if(id==48&&(state.PropertyTool()==Tool::Rectangle||state.PropertyTool()==Tool::Arrow)){
        popup.items={{L"实线",0},{L"虚线",1},{L"点线",2},{L"点划线",3}};current=static_cast<int>(state.line_style);
    }else if(id==49&&state.PropertyTool()==Tool::Arrow){
        for(int i=0;i<6;++i)popup.items.emplace_back(ArrowTypeNames[i],i);current=static_cast<int>(state.arrow_type);
    }else if(id==50&&state.PropertyTool()==Tool::Arrow){numeric({4,8,12,16,24,32,48,64});current=int(state.arrow_size);
    }else if(id==24&&state.PropertyTool()==Tool::Mosaic){numeric({16,24,32,48,64,96});current=int(state.mosaic_brush);
    }else if(id==27&&state.PropertyTool()!=Tool::Select){
        if(state.PropertyTool()==Tool::Text){numeric({8,12,16,20,24,28,32,36,48,64,72,96});current=int(state.font_size);}
        else if(state.PropertyTool()==Tool::Mosaic){numeric({8,12,20,32});current=int(state.mosaic_cell);}
        else if(state.Highlighting()){numeric({8,12,16,24,32,48,64});current=int(state.ActiveWidth());}
        else {numeric({1,2,4,6,8});current=int(state.ActiveWidth());}
    }else return false;
    for(int i=0;i<static_cast<int>(popup.items.size());++i)if(popup.items[i].second==current)popup.selected=i;
    popup.hover=popup.selected>=0?popup.selected:0;
    const auto anchor=state.toolbar.Property(id,state.PropertyTool());
    popup.Place(anchor,monitor,state.toolbar.scale,id==49||id==66||id==80?240.f:156.f);state.dropdown=std::move(popup);return true;
}
void ChooseToolbarDropdown(ViewState& state,int index) {
    auto& popup=state.dropdown;
    if(!state.busy&&index>=0&&index<static_cast<int>(popup.items.size())){
        const int value=popup.items[index].second;
        switch(popup.property){
        case 80:if(value>=0&&value<static_cast<int>(TextFonts().size()))state.font_family=TextFonts()[value].family;break;

        case 71:state.number_text_size=float(value);break;
        case 69:state.number_combo=static_cast<NumberCombo>(value);break;
        case 67:state.number_shape=static_cast<NumberShape>(value);break;
        case 68:state.number_size=float(value);break;
        case 57:state.pen_smoothing=static_cast<PenSmoothing>(value);break;
        case 52:state.text_background=static_cast<TextBackground>(value);break;
        case 48:state.line_style=static_cast<LineStyle>(value);break;
        case 49:state.arrow_type=static_cast<ArrowType>(value);break;
        case 66:state.arrow_head=static_cast<ArrowHead>(value);break;
        case 50:state.arrow_size=float(value);break;
        case 24:state.mosaic_brush=float(value);break;
        case 27:
            if(state.PropertyTool()==Tool::Text)state.font_size=float(value);
            else if(state.PropertyTool()==Tool::Mosaic)state.mosaic_cell=float(value);
            else state.ActiveWidth()=float(value);break;
        default:break;
        }
    }
    popup.Close();
}
}



