// Included inside SelectedPropertiesTest to exercise the real pointer path.
static Mark DashedBoxFixture(int corner,bool custom){
    auto m=NumberFixture(NumberCombo::DashedBox,custom);const Box box{260,220,550,380};
    const bool right=corner%2!=0,bottom=corner>=2;const Point c{right?box.right:box.left,bottom?box.bottom:box.top};
    m.a={c.x-36,c.y-24};m.b={c.x+36,c.y+24};m.number_target={right?box.left:box.right,bottom?box.top:box.bottom};
    if(custom)m.number_detail=box;else m.number_detail.reset();
    if(corner%2){m.number_label=L"A1";FitNumberBadge(m);}return m;
}
template<class Expect> static void DashedBoxCases(Application& app,Application::View& view,Expect expect){
    auto& state=app.state_;
    for(int corner=0;corner<4;++corner)for(bool custom:{false,true})for(float angle:{0.f,30.f,-45.f}){
        auto base=DashedBoxFixture(corner,custom);base.rotation=angle;
        for(float origin:{0.f,-800.f}){
            auto original=base;Translate(original,{origin,origin/2});const auto detail=NumberDetailBounds(original);
            const Point start=WorldPoint(original,{detail.left+(detail.right-detail.left)*.25f,detail.top});
            expect(HitMark(original,start)&&HitMarkPart(original,start)==EditPart::Detail,"dashed edge hits the frame detail, not the badge");
            expect(EditMark(original,EditPart::Detail,-1,start,start,false,false)==original,"zero-motion dashed drag keeps inferred geometry exact");
            auto expected=original;Translate(expected,{60,40});
            const auto moved=EditMark(original,EditPart::Detail,-1,start,{start.x+60,start.y+40},false,false);
            expect(moved==expected&&NumberAttached(moved),"dashed frame drag translates badge, target and frame together at every corner and rotation");
            for(int handle=0;handle<8;++handle)for(bool proportional:{false,true}){
                const auto controls=EditHandles(original,EditPart::Detail,1.5f);const auto grip=controls[handle].point;
                const auto changed=EditMark(original,EditPart::Detail,handle,grip,{grip.x+24,grip.y+18},proportional,false);
                expect(NumberAttached(changed)&&Near(NoteBadgeOffset(changed,corner%2!=0,corner>=2),NoteBadgeOffset(original,corner%2!=0,corner>=2)),"all dashed resize handles preserve the attached badge corner including Shift and negative origin");
                expect(changed.number_size==original.number_size&&Near({changed.b.x-changed.a.x,changed.b.y-changed.a.y},{original.b.x-original.a.x,original.b.y-original.a.y})&&changed.number_label==original.number_label,"dashed frame resize does not scale or replace its badge");
                expect(Near(EditHandles(changed,EditPart::Detail,1.5f)[(handle+4)%8].point,controls[(handle+4)%8].point),"dashed resize keeps the opposite world-space handle fixed");
                expect(EditMark(original,EditPart::Detail,handle,grip,grip,proportional,false)==original,"zero-motion dashed resize preserves exact data");
            }
        }
        for(float scale:{1.f,1.5f,2.f}){
            const auto prepare=[&]{app.document_.Reset();app.document_.marks={base};state.selected=true;state.selection={30,30,900,700};state.tool=Tool::Pen;app.UpdateToolbar({150,150});state.toolbar.scale=scale;};
            prepare();const auto detail=NumberDetailBounds(base);const Point start=WorldPoint(base,{detail.left+(detail.right-detail.left)*.25f,detail.top});
            app.PointerDown(view,start);app.PointerUp(view,start);
            expect(state.tool==Tool::Select&&app.document_.selected_part==EditPart::Detail&&app.document_.marks[0]==base&&!app.document_.CanUndo(),"clicking a dashed frame directly selects its detail without history");
            app.PointerDown(view,start);app.PointerMove(view,{start.x+20,start.y+10},MK_LBUTTON);app.PointerMove(view,{start.x+60,start.y+40},MK_LBUTTON);app.PointerUp(view,{start.x+60,start.y+40});
            auto expected=base;Translate(expected,{60,40});
            expect(app.document_.marks[0]==expected,"real multi-event dashed frame drag moves the entire combination at every DPI");
            app.Command(7);expect(app.document_.marks[0]==base&&!app.document_.CanUndo(),"dashed combination drag undoes in exactly one step");app.Command(8);expect(app.document_.marks[0]==expected,"dashed combination redo restores badge and frame together");
            const Point again{start.x+60,start.y+40};app.PointerDown(view,again);app.PointerMove(view,{again.x-20,again.y+25},MK_LBUTTON);app.PointerUp(view,{again.x-20,again.y+25});Translate(expected,{-20,25});
            expect(app.document_.marks[0]==expected,"repeated dashed frame dragging does not leave the badge behind");
            for(int handle=0;handle<8;++handle){
                prepare();state.tool=Tool::Select;app.document_.selected=0;app.document_.selected_part=EditPart::Detail;app.SyncSelectedProperties();
                const auto grip=EditHandles(base,EditPart::Detail,scale)[handle].point;
                app.PointerDown(view,grip);expect(app.mark_handle_==handle,"real pointer grabs the intended dashed frame resize handle");
                app.PointerMove(view,{grip.x+8,grip.y+6},MK_LBUTTON);app.PointerMove(view,{grip.x+24,grip.y+18},MK_LBUTTON);app.PointerUp(view,{grip.x+24,grip.y+18});
                const auto changed=app.document_.marks[0];
                expect(NumberAttached(changed)&&Near(NoteBadgeOffset(changed,corner%2!=0,corner>=2),NoteBadgeOffset(base,corner%2!=0,corner>=2))&&changed.number_size==base.number_size,"real repeated-event dashed resizing retains the badge anchor and size");
                app.Command(7);expect(app.document_.marks[0]==base&&!app.document_.CanUndo(),"dashed resize is one undo checkpoint");app.Command(8);expect(app.document_.marks[0]==changed,"dashed resize redo restores attached geometry");
            }
        }
    }
    auto mark=DashedBoxFixture(0,false);mark.color=0xffe0529c;Document doc;doc.marks={mark};
    const auto source=MakeFrame({0,0,800,560},0xfffaf9f7);const RECT display{-400,100,0,380};
    auto shown=PinAnnotationDisplayDocument(doc,source,display);const auto d=NumberDetailBounds(shown.marks[0]);const Point grip{d.left+(d.right-d.left)*.25f,d.top};
    shown.marks[0]=EditMark(shown.marks[0],EditPart::Detail,-1,grip,{grip.x+20,grip.y+15},false,false);
    const auto saved=PinAnnotationDocument(shown,display,source);auto expected=mark;Translate(expected,{40,30});
    expect(saved.marks[0]==expected&&NumberAttached(saved.marks[0]),"scaled negative-origin pinned-image frame drag preserves linked geometry on save");
    const auto folder=std::filesystem::path(L"build/dashed-box/visuals");std::filesystem::create_directories(folder);Renderer renderer;
    SavePng(renderer.Flatten(source,doc,source.bounds),folder/L"before.png");
    const auto box=NumberDetailBounds(mark);const Point start{box.left+(box.right-box.left)*.25f,box.top};
    doc.marks[0]=EditMark(mark,EditPart::Detail,-1,start,{start.x+80,start.y+60},false,false);
    SavePng(renderer.Flatten(source,doc,source.bounds),folder/L"dragged.png");
    const auto handle=EditHandles(mark,EditPart::Detail,1)[0].point;doc.marks[0]=EditMark(mark,EditPart::Detail,0,handle,{handle.x-60,handle.y-40},false,false);
    SavePng(renderer.Flatten(source,doc,source.bounds),folder/L"resized.png");
    app.document_.Reset();state.tool=Tool::Select;app.SyncSelectedProperties();app.UpdateToolbar({150,150});
}
