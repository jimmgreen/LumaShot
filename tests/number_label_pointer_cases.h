template<class Expect> static void LabelPointerCases(Application& app,Application::View& view,Expect expect){
    auto& state=app.state_;
    for(float dpi:{1.f,1.5f,2.f})for(const auto& label:{std::wstring{},std::wstring(L"步骤 A · 确认")})for(Point finish:{Point{210,300},Point{660,330},Point{420,350},Point{420,100}}){
        app.document_.Reset();state.tool=Tool::Number;state.selected_tool=Tool::Select;state.number_combo=NumberCombo::Leader;state.number_shape=NumberShape::Capsule;state.number_size=32;state.number_label=label;state.toolbar=PlaceToolbar(state.selection,view.bounds,dpi,1,Tool::Number,NumberCombo::Leader);
        const Point target{420,200};const float scale=state.toolbar.scale;
        app.PointerDown(view,target);expect(app.draft_&&app.draft_->number_target==target&&Near(BadgeCenter(*app.draft_),target),"leader mouse-down fixes target and initializes badge at pointer");
        app.PointerMove(view,{520,280},MK_LBUTTON);expect(app.draft_&&app.draft_->number_target==target&&Near(BadgeCenter(*app.draft_),{520,280}),"leader dragging moves only badge while target remains fixed");
        app.PointerUp(view,finish);expect(app.document_.marks.size()==1,"leader release creates exactly one mark");if(app.document_.marks.empty())continue;
        const auto original=app.document_.marks[0];expect(Near(BadgeCenter(original),finish)&&original.number_target==target&&original.number_label==label&&std::abs(original.number_size-32*scale)<.03f,"release applies final pointer position even without a matching last move at all DPI");
        const auto center=BadgeCenter(original);app.PointerDown(view,center);app.PointerUp(view,center);app.PointerDown(view,center);app.PointerMove(view,{center.x+35,center.y+20},MK_LBUTTON);app.PointerUp(view,{center.x+35,center.y+20});
        const auto moved=app.document_.marks[0];expect(NumberAttached(moved)&&Near(BadgeCenter(moved),{center.x+35,center.y+20})&&moved.number_target==target,"new target-first leader supports re-edit without moving remote target");
        app.Command(7);expect(app.document_.marks[0]==original,"UI undo restores prior badge position and label");app.Command(8);expect(app.document_.marks[0]==moved,"UI redo restores reconnected custom or numbered badge");
    }
    app.document_.Reset();state.tool=Tool::Number;state.selected_tool=Tool::Select;state.number_combo=NumberCombo::Leader;state.toolbar=PlaceToolbar(state.selection,view.bounds,1,1,Tool::Number,NumberCombo::Leader);
    app.PointerDown(view,{300,200});app.PointerUp(view,{302,200});expect(app.document_.marks.empty()&&!app.document_.CanUndo(),"click or tiny drag does not leave a zero-length leader or undo step");
    app.PointerDown(view,{300,200});app.PointerUp(view,{600,350});expect(app.document_.marks.size()==1&&Near(BadgeCenter(app.document_.marks[0]),{600,350})&&app.document_.marks[0].number_target==Point{300,200},"fast down/up gesture works without any intermediate mouse move");
    for(auto combo:{NumberCombo::Plain,NumberCombo::DashedBox,NumberCombo::Highlight}){
        app.document_.Reset();state.tool=Tool::Number;state.selected_tool=Tool::Select;state.number_combo=combo;state.toolbar=PlaceToolbar(state.selection,view.bounds,1,1,Tool::Number,combo);
        app.PointerDown(view,{300,200});app.PointerMove(view,{600,350},MK_LBUTTON);app.PointerUp(view,{600,350});expect(app.document_.marks.size()==1&&Near(BadgeCenter(app.document_.marks[0]),{300,200})&&app.document_.marks[0].number_target==Point{600,350},"other number combinations retain badge-first range drawing");
    }
}
