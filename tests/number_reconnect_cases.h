#pragma once
#include "model/number.h"
#include "pin/annotation.h"
#include "export/png.h"
#include <cmath>

namespace lumashot {
inline Point BadgeCenter(const Mark& m){const auto b=Normalize(m.a,m.b);return {(b.left+b.right)/2,(b.top+b.bottom)/2};}
inline Point WorldPoint(const Mark& m,Point p){return RotatePoint(p,MarkCenter(m),m.rotation);}
inline bool Near(Point a,Point b){return std::hypot(a.x-b.x,a.y-b.y)<.03f;}
inline bool NumberAttached(const Mark& m){
    const auto badge=Normalize(m.a,m.b);const auto c=BadgeCenter(m);
    if(m.number_combo==NumberCombo::Leader){const auto points=NumberLeaderPoints(m);const float dx=points[1].x-c.x;const float side=std::abs(dx)>.01f?(dx>0?1.f:-1.f):(points[0].x>=c.x?1.f:-1.f);return Near(points[0],{c.x+side*((badge.right-badge.left)/2+m.number_size*.15f),c.y});}
    const auto d=NumberDetailBounds(m);
    if(m.number_combo==NumberCombo::Highlight||m.number_combo==NumberCombo::DashedBox)return (std::abs(c.x-d.left)<.03f||std::abs(c.x-d.right)<.03f)&&(std::abs(c.y-d.top)<.03f||std::abs(c.y-d.bottom)<.03f);
    if(m.number_combo==NumberCombo::Text){const float gap=m.number_size*.2f;return std::abs(d.top-badge.top)<.03f&&(std::abs(d.left-badge.right-gap)<.03f||std::abs(badge.left-d.right-gap)<.03f);}
    return true;
}
inline Mark NumberFixture(NumberCombo combo,bool custom){
    Mark m;m.tool=Tool::Number;m.number=1;m.color=0xffff574f;m.number_shape=NumberShape::Capsule;m.number_size=48;
    m.a={90,120};m.b={162,168};m.number_target={420,260};m.number_combo=combo;m.text=L"说明文字";
    if(custom){if(combo==NumberCombo::Leader){m.number_leader=NumberLeaderPoints(m);(*m.number_leader)[1].x+=24;(*m.number_leader)[2].y+=32;}
        else {m.number_detail=NumberDetailBounds(m);if(combo==NumberCombo::Text)m.number_detail=Box{180,120,440,230};}}
    return m;
}
inline Point NoteBadgeOffset(const Mark& mark,bool right,bool bottom){
    const auto box=NumberDetailBounds(mark);
    const auto corner=WorldPoint(mark,{right?box.right:box.left,bottom?box.bottom:box.top});
    const auto badge=WorldPoint(mark,BadgeCenter(mark));return {badge.x-corner.x,badge.y-corner.y};
}
inline Mark NoteResizeFixture(int corner){
    auto mark=NumberFixture(NumberCombo::Text,true);mark.number_detail=Box{250,210,570,350};
    mark.a={corner%2?590.f:190.f,corner>=2?370.f:160.f};mark.b={mark.a.x+40,mark.a.y+40};mark.number_size=40;
    mark.number_target={570,350};if(corner%2)mark.number_label=L"看这里";mark.text=L"这里2";return mark;
}
template<class Expect> void NoteResizeCases(Expect expect){
    for(int corner=0;corner<4;++corner)for(float rotation:{0.f,30.f,-45.f})for(int handle=0;handle<8;++handle)for(bool proportional:{false,true})for(float direction:{-1.f,1.f}){
        auto original=NoteResizeFixture(corner);original.rotation=rotation;if(direction<0)Translate(original,{-800,-400});
        const auto controls=EditHandles(original,EditPart::Detail,1.5f);const auto start=controls[handle].point;
        const auto local=RotatePoint(start,MarkCenter(original),-rotation);
        const bool left=handle==0||handle==6||handle==7,right=handle==2||handle==3||handle==4,top=handle<=2,bottom=handle>=4&&handle<=6;
        const Point destination=WorldPoint(original,{local.x+(left?-40:right?40:0)*direction,local.y+(top?-30:bottom?30:0)*direction});
        const auto changed=EditMark(original,EditPart::Detail,handle,start,destination,proportional,false);
        const auto offset=NoteBadgeOffset(original,corner%2!=0,corner>=2);
        expect(Near(NoteBadgeOffset(changed,corner%2!=0,corner>=2),offset),"every note resize handle preserves badge-to-corner offset across rotation and negative origin");
        expect(Near({changed.b.x-changed.a.x,changed.b.y-changed.a.y},{original.b.x-original.a.x,original.b.y-original.a.y})&&changed.number_size==original.number_size&&changed.number_label==original.number_label&&changed.number==original.number,"note resize moves badge without scaling its size or changing its caption");
        expect(Near(EditHandles(changed,EditPart::Detail,1.5f)[(handle+4)%8].point,controls[(handle+4)%8].point),"note resize preserves opposite world-space handle");
        expect(EditMark(original,EditPart::Detail,handle,start,start,proportional,false)==original,"zero-motion note resize preserves exact original geometry");
        const auto nextStart=EditHandles(changed,EditPart::Detail,1.5f)[handle].point;
        const auto repeated=EditMark(changed,EditPart::Detail,handle,nextStart,{nextStart.x+10,nextStart.y+8},proportional,false);
        expect(Near(NoteBadgeOffset(repeated,corner%2!=0,corner>=2),offset),"repeated note resize does not accumulate badge separation");
        Document doc;doc.marks={original};doc.Checkpoint();doc.marks[0]=changed;doc.Undo();expect(doc.marks[0]==original,"linked resize undoes badge and note together");doc.Redo();expect(doc.marks[0]==changed,"linked resize redoes badge and note together");
    }
    // Inferred standard notes must follow their top-left anchor too.
    auto inferred=NumberFixture(NumberCombo::Text,false);const auto start=EditHandles(inferred,EditPart::Detail,1)[0].point;
    const auto changed=EditMark(inferred,EditPart::Detail,0,start,{start.x-45,start.y-25},false,false);
    expect(Near(NoteBadgeOffset(changed,false,false),NoteBadgeOffset(inferred,false,false))&&changed.number_detail.has_value(),"inferred note becomes explicitly resized while retaining badge offset");
    const auto folder=std::filesystem::path(L"build/note-resize-preview");std::filesystem::create_directories(folder);
    auto mark=NoteResizeFixture(1);mark.number_label.clear();mark.number=2;mark.color=0xffe0529c;const auto grip=EditHandles(mark,EditPart::Detail,2)[2].point;
    const auto resized=EditMark(mark,EditPart::Detail,2,grip,{grip.x+70,grip.y-60},false,false);
    auto frame=MakeFrame({0,0,800,480},0xff222a30);Document doc;Renderer renderer;doc.marks={mark};SavePng(renderer.Flatten(frame,doc,frame.bounds),folder/L"before.png");doc.marks={resized};SavePng(renderer.Flatten(frame,doc,frame.bounds),folder/L"after.png");
}
template<class Expect> void NumberReconnectModelCases(Expect expect){
    NoteResizeCases(expect);
    // A text-note body must never split away from its badge (including placeholder text).
    for(bool custom:{false,true})for(bool caption:{false,true})for(bool empty:{false,true})for(float rotation:{0.f,30.f,-45.f})for(float origin:{0.f,-600.f}){
        auto original=NumberFixture(NumberCombo::Text,custom);original.rotation=rotation;
        if(caption)original.number_label=L"看这里";
        if(empty)original.text.clear();
        Translate(original,{origin,0});
        const auto detail=NumberDetailBounds(original);
        const Point grip=WorldPoint(original,{(detail.left+detail.right)/2,(detail.top+detail.bottom)/2});
        expect(HitMark(original,grip)&&HitMarkPart(original,grip)==EditPart::Detail,"text-note body hit remains distinct from badge for double-click editing");
        expect(EditMark(original,EditPart::Detail,-1,grip,grip,false,false)==original,"clicking a note body does not freeze inferred geometry");
        for(Point delta:{Point{55,35},Point{-120,70},Point{40,-80}}){
            const Point destination{grip.x+delta.x,grip.y+delta.y};
            auto expected=original;Translate(expected,{destination.x-grip.x,destination.y-grip.y});
            const auto moved=EditMark(original,EditPart::Detail,-1,grip,destination,false,false);
            expect(moved==expected,"text body translates entire badge-note combination without changing relative layout or styles");
            expect(Near(WorldPoint(moved,BadgeCenter(moved)),{WorldPoint(original,BadgeCenter(original)).x+delta.x,WorldPoint(original,BadgeCenter(original)).y+delta.y}),"badge follows note body drag in rotated physical coordinates");
            Document doc;doc.marks={original};doc.Checkpoint();doc.marks[0]=moved;doc.Undo();expect(doc.marks[0]==original,"note group movement undoes atomically");doc.Redo();expect(doc.marks[0]==moved,"note group movement redoes atomically");
        }
        const auto controls=EditHandles(original,EditPart::Detail,1.5f);const auto corner=controls[4].point;
        const auto resized=EditMark(original,EditPart::Detail,4,corner,{corner.x+24,corner.y+16},false,false);
        expect(Near(WorldPoint(resized,BadgeCenter(resized)),WorldPoint(original,BadgeCenter(original))),"opposite bottom-right resize keeps a top-left anchored badge fixed");
    }
    for(auto combo:{NumberCombo::Leader,NumberCombo::DashedBox,NumberCombo::Highlight,NumberCombo::Text})for(bool custom:{false,true})for(float rotation:{0.f,30.f,-45.f}){
        auto original=NumberFixture(combo,custom);original.rotation=rotation;
        for(Point delta:{Point{-210,60},Point{360,-80},Point{30,90}}){
            const auto start=WorldPoint(original,BadgeCenter(original));
            const auto moved=EditMark(original,EditPart::Badge,-1,start,{start.x+delta.x,start.y+delta.y},false,false);
            expect(NumberAttached(moved),"moved badge reconnects inferred and custom components across directions/rotation");
            expect(Near(WorldPoint(moved,BadgeCenter(moved)),{start.x+delta.x,start.y+delta.y}),"badge follows the pointer in physical coordinates");
            if(combo!=NumberCombo::Text)expect(Near(WorldPoint(moved,moved.number_target),WorldPoint(original,original.number_target)),"far target stays fixed in world space");
            if(combo==NumberCombo::Leader&&custom)expect(Near(WorldPoint(moved,NumberLeaderPoints(moved)[2]),WorldPoint(original,NumberLeaderPoints(original)[2])),"manual remote elbow stays fixed");
            if(combo==NumberCombo::Text&&custom){const auto a=NumberDetailBounds(original),b=NumberDetailBounds(moved);expect(std::abs(a.right-a.left-b.right+b.left)<.03f&&std::abs(a.bottom-a.top-b.bottom+b.top)<.03f,"custom note retains width and height when docking");}
            Document doc;doc.marks={original};doc.Checkpoint();doc.marks[0]=moved;doc.Undo();expect(doc.marks[0]==original,"undo restores complete original attachment");doc.Redo();expect(doc.marks[0]==moved,"redo restores the connected combination");
            const auto source=MakeFrame({0,0,800,500},0xfff8f8f8);Document document;document.marks={moved};const RECT display{-400,100,0,350};const auto restored=PinAnnotationDocument(PinAnnotationDisplayDocument(document,source,display),display,source);
            expect(NumberAttached(restored.marks[0]),"connection survives scaled negative-origin pin edit roundtrip");
        }
        for(int handle=0;handle<8;++handle){const auto hs=EditHandles(original,EditPart::Badge,1);const auto start=hs[handle].point;const auto resized=EditMark(original,EditPart::Badge,handle,start,{start.x+22,start.y+16},false,false);expect(NumberAttached(resized),"badge resize handles keep components attached");}
        const auto start=WorldPoint(original,BadgeCenter(original));expect(EditMark(original,EditPart::Badge,-1,start,start,false,false)==original,"zero-motion edit preserves exact mark data");
        const auto movedWhole=EditMark(original,EditPart::Whole,-1,{0,0},{20,30},false,false);auto expected=original;Translate(expected,{20,30});expect(movedWhole==expected,"whole-combination move remains unchanged");
    }
    auto manual=NumberFixture(NumberCombo::Leader,true);const auto points=NumberLeaderPoints(manual);auto elbow=EditMark(manual,EditPart::Leader,21,points[1],{points[1].x+12,points[1].y+20},false,false);
    expect(elbow.a==manual.a&&Near(NumberLeaderPoints(elbow)[1],{points[1].x+12,points[1].y+20}),"independent elbow editing remains available");
    // Dragging the leader line body moves badge and line together as one group.
    for(bool custom:{false,true})for(float rotation:{0.f,30.f,-45.f})for(Point delta:{Point{-120,45},Point{200,-60},Point{16,110}}){
        auto original=NumberFixture(NumberCombo::Leader,custom);original.rotation=rotation;
        const auto line=NumberLeaderPoints(original);
        const Point grip=WorldPoint(original,{(line[1].x+line[2].x)/2,(line[1].y+line[2].y)/2});
        const Point destination{grip.x+delta.x,grip.y+delta.y};
        const auto moved=EditMark(original,EditPart::Leader,-1,grip,destination,false,false);
        auto expected=original;Translate(expected,{destination.x-grip.x,destination.y-grip.y});
        expect(moved==expected,"dragging the leader line body moves the whole combination as one group");
        expect(NumberAttached(moved),"leader stays attached to its badge after a line-body drag");
        Document doc;doc.marks={original};doc.Checkpoint();doc.marks[0]=moved;doc.Undo();expect(doc.marks[0]==original,"line-body group move undoes as one step");doc.Redo();expect(doc.marks[0]==moved,"redo restores the group-moved combination");
    }
    // Every editable vertex must preserve the badge attachment, not only line-body drags.
    for(bool custom:{false,true})for(float rotation:{0.f,30.f,-45.f})for(Point delta:{Point{-190,100},Point{-420,-80},Point{370,110}})for(int handle=20;handle<=23;++handle){
        auto original=NumberFixture(NumberCombo::Leader,custom);original.rotation=rotation;
        const auto start=EditHandles(original,EditPart::Leader,1)[handle-20].point;
        const Point destination{start.x+delta.x,start.y+delta.y};
        const auto changed=EditMark(original,EditPart::Leader,handle,start,destination,false,false);
        expect(NumberAttached(changed),"every leader vertex drag retains badge attachment");
        if(handle==20){
            const auto c=WorldPoint(original,BadgeCenter(original));
            expect(Near(WorldPoint(changed,BadgeCenter(changed)),{c.x+delta.x,c.y+delta.y}),"attached endpoint drag moves badge with pointer");
            expect(Near(WorldPoint(changed,changed.number_target),WorldPoint(original,original.number_target)),"attached endpoint drag keeps target fixed");
        }else{
            expect(Near(WorldPoint(changed,NumberLeaderPoints(changed)[handle-20]),destination),"free elbow or target follows pointer");
            expect(Near(WorldPoint(changed,BadgeCenter(changed)),WorldPoint(original,BadgeCenter(original))),"free vertex leaves badge fixed");
        }
        expect(EditMark(original,EditPart::Leader,handle,start,start,false,false)==original,"vertex click without motion preserves exact data");
        Document doc;doc.marks={original};doc.Checkpoint();doc.marks[0]=changed;doc.Undo();expect(doc.marks[0]==original,"vertex undo restores original");doc.Redo();expect(doc.marks[0]==changed,"vertex redo restores attached result");
    }
    // Reproduce a legacy detached line exactly: geometry was frozen before its badge moved.
    auto broken=NumberFixture(NumberCombo::Leader,true);broken.a={510,160};broken.b={582,208};
    expect(!NumberAttached(broken),"legacy detached fixture reproduces the reported issue");const auto center=BadgeCenter(broken);auto repaired=EditMark(broken,EditPart::Badge,-1,center,{center.x-20,center.y+35},false,false);expect(NumberAttached(repaired),"next badge drag repairs previously detached stored geometry");
    const std::filesystem::path folder=L"build/number-reconnect-preview";std::filesystem::create_directories(folder);
    auto canvas=MakeFrame({0,0,720,400},0xfffaf9f7);Renderer renderer;Document doc;doc.marks={broken};SavePng(renderer.Flatten(canvas,doc,canvas.bounds),folder/L"leader-before.png");doc.marks={repaired};SavePng(renderer.Flatten(canvas,doc,canvas.bounds),folder/L"leader-after.png");
    auto attached=NumberFixture(NumberCombo::Leader,true);const auto grip=NumberLeaderPoints(attached)[0];const Point destination{grip.x+120,grip.y+90};
    auto detached=attached;(*detached.number_leader)[0]=destination;doc.marks={detached};SavePng(renderer.Flatten(canvas,doc,canvas.bounds),folder/L"vertex-before.png");
    doc.marks={EditMark(attached,EditPart::Leader,20,grip,destination,false,false)};SavePng(renderer.Flatten(canvas,doc,canvas.bounds),folder/L"vertex-after.png");
    for(auto combo:{NumberCombo::DashedBox,NumberCombo::Highlight,NumberCombo::Text}){auto m=NumberFixture(combo,true);const auto c=BadgeCenter(m);m=EditMark(m,EditPart::Badge,-1,c,{c.x+80,c.y+50},false,false);doc.marks={m};SavePng(renderer.Flatten(canvas,doc,canvas.bounds),folder/(std::to_wstring(static_cast<int>(combo))+L"-after.png"));}
}
}
