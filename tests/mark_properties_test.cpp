#include "model/mark_properties.h"
#include <cmath>
#include <iostream>
using namespace lumashot;
int main(){
    int failures=0;const auto expect=[&](bool ok,const char* label){if(!ok){std::cout<<"FAIL "<<label<<'\n';++failures;}};
    const auto approximately=[](float a,float b){return std::abs(a-b)<.001f;};
    for(float scale:{1.f,1.5f,2.f})for(Tool tool:{Tool::Select,Tool::Rectangle,Tool::Ellipse,Tool::Arrow,Tool::Pen,Tool::Text,Tool::Mosaic,Tool::Number}){
        Mark m;m.tool=tool;m.a={19.3f,28.4f};m.b={171.7f,105.9f};m.rotation=.47f;m.width=7.12345f;m.font_size=27.7123f;m.arrow_size=13.713f;m.number_size=37.71f;m.number_text_size=17.91f;m.mosaic_cell=29.713f;m.mosaic_brush=71.123f;m.corner_radius=8.173f;
        m.corner_radii=std::array<float,4>{1,2,3,4};m.number_detail=Box{200,210,300,310};m.number_leader=std::array<Point,4>{Point{1,2},Point{3,4},Point{5,6},Point{7,8}};m.points={{20,30},{44,55},{90,60}};m.text=L"Synthetic annotation";m.number=42;m.number_target={260,310};
        auto p=PropertiesOfMark(m,scale,{});expect(WithMarkProperties(m,p,scale)==m,"all tool properties round trip exactly at every DPI");
        p.font_family=L"Arial";p.mosaic_cell=991;p.number_text_preset=17;
        if(tool!=Tool::Text&&tool!=Tool::Number)expect(WithMarkProperties(m,p,scale)==m,"irrelevant properties leave mark unchanged");
        p=PropertiesOfMark(m,scale,{});
        if(tool==Tool::Rectangle||tool==Tool::Ellipse||tool==Tool::Arrow||tool==Tool::Pen){p.width=11;auto edited=WithMarkProperties(m,p,scale);auto wanted=m;wanted.width=11*scale;expect(edited==wanted,"width edit preserves all geometry and unrelated styles");}
        if(tool==Tool::Rectangle){p=PropertiesOfMark(m,scale,{});p.corner_radius=15;auto wanted=m;wanted.corner_radius=15*scale;wanted.corner_radii.reset();expect(WithMarkProperties(m,p,scale)==wanted,"corner radius edit alone clears individual radii");}
        if(tool==Tool::Pen){m.pen_mode=PenMode::Highlighter;ToolProperties defaults;defaults.color=0xff123456;defaults.width=3;defaults.pen_opacity=.9f;p=PropertiesOfMark(m,scale,defaults);expect(p.color==defaults.color&&p.width==defaults.width&&p.pen_opacity==defaults.pen_opacity,"highlighter keeps normal pen defaults");expect(WithMarkProperties(m,p,scale)==m,"highlighter exact round trip");p.highlighter_width=31;p.highlighter_opacity=.2f;auto wanted=m;wanted.width=31*scale;wanted.pen_opacity=.2f;expect(WithMarkProperties(m,p,scale)==wanted,"highlighter edits preserve stroke points");}
        if(tool==Tool::Text){p.font_size=33;p.text_bold=true;auto wanted=m;wanted.font_size=33*scale;wanted.text_bold=true;expect(WithMarkProperties(m,p,scale)==wanted,"rotated explicit text box retains geometry");m.text_auto_size=true;p=PropertiesOfMark(m,scale,{});expect(WithMarkProperties(m,p,scale)==m,"unchanged auto size text does not remeasure");p.font_size=40;auto edited=WithMarkProperties(m,p,scale);expect(edited.font_size==40*scale&&edited.rotation==m.rotation&&edited.text==m.text&&edited.b.y>edited.a.y,"auto size text font edit measures usable bounds");}
        if(tool==Tool::Mosaic){p.mosaic_strength=65;auto wanted=m;wanted.mosaic_cell=30*scale;expect(WithMarkProperties(m,p,scale)==wanted,"mosaic strength converts to physical cell size");}
        if(tool==Tool::Number){
            // Resize a valid plain badge, not a badge mixed with pen points and an unrelated leader.
            // Linked/rotated component reconnection is covered by NumberReconnectModelCases.
            auto badge=m;badge.number_combo=NumberCombo::Plain;badge.number_detail.reset();badge.number_leader.reset();badge.points.clear();
            p=PropertiesOfMark(badge,scale,{});p.number_shape=NumberShape::Pin;p.number_size=44;auto edited=WithMarkProperties(badge,p,scale);
            expect(approximately(edited.a.x+edited.b.x,badge.a.x+badge.b.x)&&approximately(edited.a.y+edited.b.y,badge.a.y+badge.b.y)&&approximately(edited.b.y-edited.a.y,44*scale*1.3f),"plain number badge resize preserves center and pin aspect");
            auto wanted=badge;wanted.a=edited.a;wanted.b=edited.b;wanted.number_size=44*scale;wanted.number_shape=NumberShape::Pin;
            expect(edited==wanted,"plain badge edit preserves target number rotation and unrelated styles");
        }
    }
    std::cout<<(failures?"FAIL":"PASS")<<" selected mark property mapping\n";return failures?1:0;
}

