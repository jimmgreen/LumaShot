template<class Expect> static void TagEditorCases(Application& app,Application::View& view,Expect expect){
    app.document_.Reset();auto& state=app.state_;state.tool=Tool::Text;state.toolbar.scale=1;state.font_family=L"Segoe UI";state.font_size=24;state.text_align=TextAlign::Left;state.text_background=TextBackground::TagAutomatic;state.color=AnnotationColors[4];
    for(int y=0;y<app.frame_->Height();++y)for(int x=0;x<app.frame_->Width();++x)app.frame_->pixels[size_t(y)*app.frame_->Width()+x]=((x/12+y/12)%2)?0xffe8eef4:0xffc6d2df;
    app.BeginText(view,{150,150});expect(app.edit_&&app.edit_surface_color_==TextEditorFrame::Background(state.dark),"tag input keeps the original solid theme background");
    if(!app.edit_)return;
    SetWindowTextW(app.edit_,L"Tag 输入预览\r\n第二行");Pump();SendMessageW(app.edit_,EM_SETSEL,static_cast<WPARAM>(-1),-1);
    RECT rect{};GetClientRect(app.edit_,&rect);DibSurface pixels(rect.right,rect.bottom);PrintWindow(app.edit_host_,pixels.Dc(),0);GdiFlush();
    const auto a=pixels.Pixels()[size_t(rect.bottom-2)*rect.right+80]&0xffffff,b=pixels.Pixels()[size_t(rect.bottom-2)*rect.right+92]&0xffffff;
    expect(a==b&&a==(TextEditorFrame::Background(state.dark)&0xffffff),"native EDIT background does not reveal screenshot texture");
    SendMessageW(app.edit_,WM_IME_STARTCOMPOSITION,0,0);Key(app.edit_,VK_ESCAPE);expect(app.edit_!=nullptr,"tag preview preserves native IME composition and Escape handling");
    SendMessageW(app.edit_,WM_IME_ENDCOMPOSITION,0,0);
    Key(app.edit_,VK_RETURN);
    expect(app.document_.marks.size()==1&&app.document_.marks[0].a==Point{150,150},"new tag commits at its original outer anchor, without double padding");
    const auto saved=app.document_.marks[0];expect(saved.text_background==TextBackground::TagAutomatic&&saved.color==AnnotationColors[4]&&(*ResolveTextAppearance(*app.frame_,saved).background>>24)<255,"theme input leaves committed Tag hue and translucent output unchanged");
    app.BeginText(view,saved.a,0);SetWindowTextW(app.edit_,L"取消");Key(app.edit_,VK_ESCAPE);expect(app.document_.marks[0]==saved,"Escape preserves tag layout and colors");
    app.BeginText(view,saved.a,0);Key(app.edit_,VK_RETURN);expect(app.document_.marks[0]==saved,"reopening a tag does not accumulate padding or change its hue");
    app.BeginText(view,saved.a,0);SetWindowTextW(app.edit_,L"短");Key(app.edit_,VK_RETURN);expect(app.document_.marks[0].b.x<saved.b.x&&app.document_.marks[0].color==saved.color,"editing shorter text tightens the tag without changing the stored hue");
    state.toolbar.scale=1.5f;app.BeginText(view,{150,260});SetWindowTextW(app.edit_,L"150% Tag");Pump();expect(app.edit_size_==36&&app.edit_surface_color_==TextEditorFrame::Background(state.dark),"high DPI input keeps scaled metrics and theme background");
    const auto image=Preview(app);std::filesystem::create_directories(L"build/input-theme-preview");SavePng(image,L"build/input-theme-preview/light.png");app.CommitText(true);
    expect(!app.edit_&&!app.edit_backdrop_,"native editing resources are released on cancel");
    for(bool dark:{false,true})for(auto mode:{TextBackground::None,TextBackground::ToneLight,TextBackground::ToneDark,TextBackground::Automatic,TextBackground::TagAutomatic,TextBackground::TagLight,TextBackground::TagDark}){
        state.dark=dark;state.text_background=mode;app.BeginText(view,{150,260});
        expect(app.edit_&&app.edit_surface_color_==TextEditorFrame::Background(dark),"all text modes use application theme for input, not annotation fill");
        if(app.edit_){SetWindowTextW(app.edit_,L"150% Tag");Pump();if(dark&&mode==TextBackground::TagAutomatic)SavePng(Preview(app),L"build/input-theme-preview/dark.png");app.CommitText(true);}
    }
    Mark note;note.tool=Tool::Number;note.number_combo=NumberCombo::Text;note.a={150,150};note.b={182,182};note.number_target={450,230};note.text=L"说明";app.document_.Reset();app.document_.Add(note);
    for(bool dark:{false,true}){state.dark=dark;app.BeginText(view,note.a,0);expect(app.edit_&&app.edit_surface_color_==TextEditorFrame::Background(dark),"number note input also retains solid theme background");app.CommitText(true);}
    // Reproduce dragging the placeholder under a custom caption, then double-click editing it.
    app.document_.marks[0].number_label=L"看这里";app.document_.marks[0].text.clear();state.tool=Tool::Select;
    const auto grouped=app.document_.marks[0];const auto detail=NumberDetailBounds(grouped);
    const Point grip{(detail.left+detail.right)/2,(detail.top+detail.bottom)/2},destination{grip.x+35,grip.y+25};
    app.PointerDown(view,grip);app.PointerMove(view,destination,MK_LBUTTON);app.PointerUp(view,destination);
    auto expected=grouped;Translate(expected,{35,25});expect(app.document_.marks[0]==expected,"dragging placeholder moves its custom caption too");
    app.PointerDoubleClick(view,destination);
    expect(app.edit_&&app.edit_index_==0&&!app.edit_badge_label_,"double click moved note still opens description editor, not caption editor");
    if(app.edit_){SetWindowTextW(app.edit_,L"组合移动后编辑说明");Key(app.edit_,VK_RETURN);}
    expect(app.document_.marks[0].text==L"组合移动后编辑说明"&&app.document_.marks[0].number_label==L"看这里"&&app.document_.marks[0].a==expected.a&&app.document_.marks[0].b==expected.b,"editing note after group movement preserves caption and badge position");
}
