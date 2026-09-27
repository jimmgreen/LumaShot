#pragma once
#include "number_reconnect_cases.h"
#include "model/number_label.h"
#include "model/mark_properties.h"
#include "app/tool_preferences.h"

namespace lumashot {
template<class Expect> void NumberLabelCases(Expect expect){
    auto base=NumberFixture(NumberCombo::Leader,true);base.number=12;base.number_shape=NumberShape::Pin;
    const auto original=base;
    auto properties=PropertiesOfMark(base,1,{});properties.number_label=L"登录入口";
    auto label=WithMarkProperties(base,properties,1);
    expect(label.number_label==L"登录入口"&&label.text==base.text&&NumberCaption(label)==L"登录入口","badge label is separate from description and automatic number");
    expect(Near(BadgeCenter(base),BadgeCenter(label))&&NumberAttached(label)&&label.number_target==base.number_target,"label width change keeps badge center and remote target with explicit attachment");
    expect(label.b.x-label.a.x>label.b.y-label.a.y&&std::abs(label.b.y-label.a.y-label.number_size)<.03f,"custom text gets an adaptive capsule even when automatic shape is pin");
    properties.number_label=L"iiii";auto narrow=WithMarkProperties(base,properties,1);properties.number_label=L"WWWW";auto wide=WithMarkProperties(base,properties,1);
    expect(wide.b.x-wide.a.x>narrow.b.x-narrow.a.x+20,"real glyph measurement distinguishes equal character counts");
    properties.number_label=L"";auto cleared=WithMarkProperties(label,properties,1);
    expect(cleared.number_label.empty()&&cleared.number==12&&NumberCaption(cleared)==L"12"&&cleared.number_shape==NumberShape::Pin&&cleared.text==base.text,"clearing restores assigned number and original shape without deleting description");
    expect(std::abs(cleared.b.x-cleared.a.x-cleared.number_size)<.03f&&std::abs(cleared.b.y-cleared.a.y-cleared.number_size*1.3f)<.03f,"clearing restores numeric badge dimensions");
    expect(NormalizeNumberLabel(L" \r\n ").empty()&&NormalizeNumberLabel(L" A\tB\n ")==L"A B","single line label normalizes blank input and controls");
    const std::wstring emoji=L"\U0001f680";expect(NormalizeNumberLabel(std::wstring(63,L'A')+emoji)==std::wstring(63,L'A'),"UTF-16 length cap never leaves a dangling surrogate");
    expect(NormalizeNumberLabel(std::wstring(62,L'A')+emoji+L"B").size()==64,"complete surrogate pair fits the 64-unit boundary");
    for(auto combo:{NumberCombo::Leader,NumberCombo::DashedBox,NumberCombo::Highlight,NumberCombo::Text})for(bool custom:{false,true})for(float angle:{0.f,30.f,-45.f}){
        auto before=NumberFixture(combo,custom);before.rotation=angle;
        const auto center=WorldPoint(before,BadgeCenter(before)),target=WorldPoint(before,before.number_target);
        const auto endpoint=WorldPoint(before,NumberLeaderPoints(before)[2]);
        auto p=PropertiesOfMark(before,1.5f,{});p.number_label=L"步骤 A · 确认";p.number_size=40;
        auto after=WithMarkProperties(before,p,1.5f);
        expect(after.number_label==p.number_label&&std::abs(after.number_size-60)<.03f&&NumberAttached(after),"DIP label properties reconnect every combination and rotation");
        expect(Near(WorldPoint(after,BadgeCenter(after)),center),"resizing label preserves its world-space badge center");
        if(combo!=NumberCombo::Text)expect(Near(WorldPoint(after,after.number_target),target),"label property change preserves remote target in world space");
        if(combo==NumberCombo::Leader&&custom)expect(Near(WorldPoint(after,NumberLeaderPoints(after)[2]),endpoint),"custom remote elbow survives label widening and rotation");
        if(combo==NumberCombo::Text&&custom){const auto a=NumberDetailBounds(before),b=NumberDetailBounds(after);expect(std::abs(a.right-a.left-b.right+b.left)<.03f&&std::abs(a.bottom-a.top-b.bottom+b.top)<.03f,"label change preserves custom description panel size");}
        const auto start=WorldPoint(after,BadgeCenter(after));auto moved=EditMark(after,EditPart::Badge,-1,start,{start.x+35,start.y+45},false,false);
        expect(NumberAttached(moved)&&Near(WorldPoint(moved,BadgeCenter(moved)),{start.x+35,start.y+45}),"custom capsule moves with pointer and reconnects");
        for(int handle=0;handle<8;++handle){const auto handles=EditHandles(after,EditPart::Badge,1);const auto at=handles[handle].point;const auto resized=EditMark(after,EditPart::Badge,handle,at,{at.x+12,at.y+15},false,false);expect(NumberAttached(resized)&&resized.number_label==after.number_label,"all badge resize handles retain custom text and connection");expect(std::abs(resized.number_size-after.number_size)>.01f&&std::abs(resized.b.y-resized.a.y-resized.number_size)<.03f,"every label handle scales text and adaptive background rather than only translating it");if(combo!=NumberCombo::Text)expect(Near(WorldPoint(resized,resized.number_target),WorldPoint(after,after.number_target)),"label resize handle preserves world target");}
        auto same=PropertiesOfMark(after,1.5f,{});expect(WithMarkProperties(after,same,1.5f)==after,"unchanged label properties are exact no-op");
        Document doc;doc.marks={before};doc.Checkpoint();doc.marks[0]=after;doc.Undo();expect(doc.marks[0]==before,"undo restores automatic number with old geometry");doc.Redo();expect(doc.marks[0]==after,"redo restores full custom label geometry");
        auto source=MakeFrame({0,0,800,500},0xfff8f8f8);const RECT display{-400,100,0,350};auto restored=PinAnnotationDocument(PinAnnotationDisplayDocument(doc,source,display),display,source);
        expect(restored.marks[0].number_label==after.number_label&&NumberAttached(restored.marks[0])&&Near(WorldPoint(restored.marks[0],restored.marks[0].number_target),WorldPoint(after,after.number_target)),"pin edit roundtrip preserves custom text and scaled target geometry");
    }
    const auto path=std::filesystem::temp_directory_path()/(L"LumaShot-label-"+std::to_wstring(GetCurrentProcessId())+L".ini");
    struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code e;std::filesystem::remove(p,e);}}cleanup{path};
    ToolProperties tools;for(const auto& text:{std::wstring{},std::wstring(L"\"登录=A; [B]\""),std::wstring(L"步骤 \U0001f680"),std::wstring(64,L'W')}){tools.number_label=text;SaveToolProperties(path,tools);expect(LoadToolProperties(path)==tools,"INI label roundtrip preserves Chinese, quotes, separators, emoji and maximum length");}
    WritePrivateProfileStringW(L"Tools",L"number_label_hex",L"oops",path.c_str());expect(LoadToolProperties(path).number_label.empty(),"malformed encoded label safely restores automatic numbering");
    tools.number_label.clear();SaveToolProperties(path,tools);expect(LoadToolProperties(path).number_label.empty(),"empty label persists as automatic number");
    for(float dpi:{1.f,1.5f,2.f})for(LONG width:{480,960,1600})for(auto combo:{NumberCombo::Leader,NumberCombo::Text}){
        const RECT viewport{0,0,width,1000};ViewState state;state.tool=Tool::Number;state.selected=true;state.number_combo=combo;state.number_label=L"登录入口";state.selection={20,20,float(width-20),600};state.toolbar=PlaceToolbar(state.selection,viewport,dpi,1,Tool::Number,combo);
        const auto controls=ToolbarControls(state,{});int fields=0;for(const auto& c:controls){if(c.id==81){++fields;expect(c.kind==ui::Kind::TextField&&c.text==state.number_label&&c.Interactive(),"custom-label property is a direct text field, not a mode dropdown");expect(c.bounds.left>=0&&c.bounds.right<=width&&c.bounds.top>=0&&c.bounds.bottom<=1000,"label field stays inside narrow and high-DPI monitor");expect(state.toolbar.Hit({(c.bounds.left+c.bounds.right)/2,(c.bounds.top+c.bounds.bottom)/2},Tool::Number)==81,"label field paint and hit geometry match");}if(c.id==67)expect(!c.enabled,"custom label explicitly uses auto rounded shape");}expect(fields==1,"one custom label input is present for leader and description combinations");
    }
    const std::filesystem::path folder=L"build/number-label-preview";std::filesystem::create_directories(folder);
    Renderer renderer;auto canvas=MakeFrame({0,0,1100,480},0xfffaf9f7);Document doc;
    for(int i=0;i<4;++i){auto m=NumberFixture(NumberCombo::Leader,false);m.number_size=44;m.number_shape=NumberShape::Capsule;m.number=i+1;m.number_label=std::array<std::wstring,4>{L"",L"A",L"登录入口",L"步骤 A · 确认"}[i];m.a=m.b={230.f,float(60+i*110)};m.number_target={70,float(70+i*110)};FitNumberBadge(m);doc.marks.push_back(m);}
    SavePng(renderer.Flatten(canvas,doc,canvas.bounds),folder/L"labels.png");
    for(auto combo:{NumberCombo::Leader,NumberCombo::Text,NumberCombo::DashedBox,NumberCombo::Highlight}){auto m=NumberFixture(combo,true);auto p=PropertiesOfMark(m,1,{});p.number_label=L"确认提交";m=WithMarkProperties(m,p,1);const auto c=BadgeCenter(m);m=EditMark(m,EditPart::Badge,-1,c,{c.x+70,c.y+40},false,false);doc.marks={m};SavePng(renderer.Flatten(canvas,doc,canvas.bounds),folder/(std::to_wstring(static_cast<int>(combo))+L"-reedit.png"));}
    expect(base==original,"measurement and property tests do not mutate their source mark");
}
}
