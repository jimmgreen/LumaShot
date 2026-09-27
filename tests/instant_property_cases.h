// Included inside the existing friend test: no new test executable or personal fixtures.
template<class Expect>
static void InstantPropertyCases(Application& app,Application::View& view,Expect expect){
    auto& s=app.state_;
    const auto choose=[&](Tool tool){app.Command(tool==Tool::Number?14:static_cast<int>(tool));};
    Mark untouched;untouched.tool=Tool::Rectangle;untouched.a={700,500};untouched.b={800,590};
    const auto prepare=[&](Tool tool,float scale){
        app.CommitText(true);if(s.picker.open)app.ClosePicker(true);s.dropdown.Close();
        app.document_.Reset();app.document_.marks={untouched};app.pin_edit_id_=0;
        static_cast<ToolProperties&>(s)=ToolProperties{};s.selected=true;s.selection={30,30,900,700};
        choose(tool);app.UpdateToolbar({150,150});s.toolbar.scale=scale;
    };
    const auto draw=[&](Point a,Point b){
        app.PointerDown(view,a);
        if(s.tool==Tool::Text){expect(app.edit_!=nullptr,"new text opens its native editor");if(app.edit_){SetWindowTextW(app.edit_,L"Synthetic text");app.CommitText();}}
        else{app.PointerMove(view,{(a.x+b.x)/2,(a.y+b.y)/2},MK_LBUTTON);app.PointerUp(view,b);
            if(app.edit_){SetWindowTextW(app.edit_,L"Synthetic note");app.CommitText();}}
    };
    for(float scale:{1.f,1.5f,2.f})for(Tool tool:{Tool::Rectangle,Tool::Ellipse,Tool::Arrow,Tool::Pen,Tool::Text,Tool::Mosaic,Tool::Number}){
        prepare(tool,scale);draw({140,140},{280,240});
        expect(app.document_.marks.size()==2&&app.document_.selected==1&&s.tool==tool&&app.HasPropertyTarget(),"drawing keeps the current tool and immediately targets the new annotation");
        if(app.document_.marks.size()!=2)continue;
        if(scale==1&&tool==Tool::Rectangle){
            std::filesystem::create_directories("instant-properties/visuals");Renderer renderer;
            SavePng(renderer.Flatten(*app.frame_,app.document_,RECT{90,90,390,310}),L"instant-properties/visuals/rectangle-before.png");
        }
        if(tool!=Tool::Mosaic){app.Command(21);expect(app.document_.marks[1].color==*ToolbarColor(21),"color edits apply to the just-created annotation without reselection");}
        switch(tool){
        case Tool::Rectangle:case Tool::Ellipse:
            app.Command(43);expect(app.document_.marks[1].fill_color==ToolbarColor(43),"fill applies immediately after drawing");
            s.width=7;app.ApplySelectedProperties();expect(app.document_.marks[1].width==7*scale,"new shape width respects drawing DPI");
            s.fill_opacity=.65f;app.ApplySelectedProperties();expect(app.document_.marks[1].fill_opacity==.65f,"fill opacity applies to the existing shape");break;
        case Tool::Arrow:
            s.arrow_head=ArrowHead::Diamond;s.arrow_size=20;app.ApplySelectedProperties();expect(app.document_.marks[1].arrow_head==ArrowHead::Diamond&&app.document_.marks[1].arrow_size==20*scale,"new arrow head and size update immediately");break;
        case Tool::Pen:
            app.Command(59);expect(app.document_.marks[1].pen_mode==PenMode::Highlighter,"new pen stroke can immediately switch to highlighter");
            s.highlighter_opacity=.45f;app.ApplySelectedProperties();expect(app.document_.marks[1].pen_opacity==.45f,"new highlighter opacity updates immediately");break;
        case Tool::Text:
            app.Command(51);expect(app.document_.marks[1].text_bold,"committed text formatting updates without reselection");
            s.font_size=30;app.ApplySelectedProperties();expect(app.document_.marks[1].font_size==30*scale,"new text font size respects DPI");break;
        case Tool::Mosaic:
            app.Command(61);s.mosaic_strength=65;app.ApplySelectedProperties();expect(app.document_.marks[1].mosaic_mode==MosaicMode::Blur&&app.document_.marks[1].mosaic_cell==30*scale,"new mosaic mode and strength apply immediately");break;
        case Tool::Number:
            s.number_shape=NumberShape::Square;s.number_size=40;app.ApplySelectedProperties();expect(app.document_.marks[1].number_shape==NumberShape::Square&&app.document_.marks[1].number_size==40*scale,"new number shape and size apply immediately");break;
        default:break;
        }
        expect(app.document_.marks[0]==untouched,"immediate properties never modify unrelated earlier annotations");
        if(scale==1&&(tool==Tool::Rectangle||tool==Tool::Text)){
            Renderer renderer;const auto path=L"instant-properties/visuals/after-"+std::to_wstring(static_cast<int>(tool))+L".png";
            SavePng(renderer.Flatten(*app.frame_,app.document_,RECT{90,90,650,350}),path);
        }
        const auto first=app.document_.marks[1];const Point next{430,400};
        expect(app.document_.HitTest(next)<0,"continuous-drawing fixture starts outside the previous annotation at every DPI");draw(next,{560,460});
        expect(s.tool==tool&&app.document_.marks.size()==3&&app.document_.selected==2,"same tool can keep drawing without clicking its toolbar button again");
        if(tool!=Tool::Mosaic)app.Command(22);else{s.mosaic_strength=20;app.ApplySelectedProperties();}
        expect(app.document_.marks[1]==first&&app.document_.marks[0]==untouched,"later property edits affect only the newest target");
        choose(Tool::Arrow);const auto before=app.document_.marks;app.Command(23);
        expect(app.document_.marks==before&&!app.HasPropertyTarget(),"explicit tool changes detach the previous target rather than restyling old marks");
    }
    prepare(Tool::Rectangle,1);draw({140,140},{280,240});const auto original=app.document_.marks[1];
    app.Command(28);s.PickerColor()=0xff123456;app.ApplySelectedProperties(true);
    expect(app.document_.marks[1].color==0xff123456,"picker previews the newly drawn mark live");app.ClosePicker(true);
    expect(app.document_.marks[1]==original,"canceling the picker restores the new mark");
    s.width=8;app.ApplySelectedProperties(true);s.width=12;app.ApplySelectedProperties(true);app.ApplySelectedProperties();
    expect(app.document_.marks[1].width==12,"continuous slider preview commits the final value");
    app.Command(7);expect(app.document_.marks[1]==original,"one undo restores a complete slider gesture");
    app.Command(7);expect(app.document_.marks.size()==1&&app.document_.marks[0]==untouched&&!app.document_.CanUndo(),"second undo removes drawing, with no empty picker or selection history");
    app.Command(8);const auto redone=app.document_.marks;app.Command(23);expect(app.document_.marks==redone,"redo cannot leave a stale implicit property target");
    prepare(Tool::Rectangle,1);draw({140,140},{280,240});const auto previous=app.document_.marks;
    app.PointerDown(view,{480,300});app.PointerUp(view,{480,300});app.Command(23);
    expect(app.document_.marks==previous&&!app.HasPropertyTarget(),"an empty new gesture does not silently retarget the previous shape");
    prepare(Tool::Number,1);s.number_combo=NumberCombo::Text;draw({140,140},{310,250});app.Command(73);
    expect(app.document_.marks[1].number_text_color==*ToolbarColor(73),"new number-note text color applies after committing its text");
    app.Command(81);expect(app.edit_index_==1,"label toolbar targets the just-created number rather than future defaults");
    if(app.edit_){SetWindowTextW(app.edit_,L"A1");app.CommitText();}
    expect(app.document_.marks[1].number_label==L"A1","custom number label edits the existing badge immediately");
    prepare(Tool::Rectangle,1);app.pin_edit_id_=77;draw({140,140},{280,240});app.Command(43);
    expect(app.document_.marks[1].fill_color==ToolbarColor(43),"pinned-image sessions share immediate property behavior");app.pin_edit_id_=0;
    const auto handle=EditHandles(app.document_.marks[1],EditPart::Whole,1)[0].point;
    app.PointerDown(view,handle);expect(s.tool==Tool::Select&&app.mark_handle_==0,"newly visible handles enter ordinary selection and resize behavior");app.PointerUp(view,handle);
    app.CommitText(true);s.dropdown.Close();app.document_.Reset();static_cast<ToolProperties&>(s)=ToolProperties{};choose(Tool::Select);app.UpdateToolbar({150,150});
}
