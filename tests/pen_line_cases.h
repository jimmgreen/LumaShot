// Synthetic fixtures only; included inside SelectedPropertiesTest.
template<class Expect>
static void PenLineCases(Application& app,Application::View& view,Expect expect){
    auto& s=app.state_;
    const auto prepare=[&](float scale,PenMode mode){
        app.document_.Reset();app.draft_.reset();app.pin_edit_id_=0;
        static_cast<ToolProperties&>(s)=ToolProperties{};
        s.selected=true;s.selection={30,30,900,700};s.tool=Tool::Select;s.selected_tool=Tool::Select;
        app.Command(static_cast<int>(Tool::Pen));app.toolbar_transition_.Reset(1);app.UpdateToolbar({150,150});
        s.toolbar.scale=scale;app.Command(mode==PenMode::Highlighter?59:58);app.Command(84);
    };
    const auto click=[&](int id){
        const auto b=s.toolbar.Property(id,Tool::Pen);const Point p{(b.left+b.right)/2,(b.top+b.bottom)/2};
        app.PointerDown(view,p);app.PointerUp(view,p);
    };
    std::filesystem::create_directories("pen-line/visuals");
    for(float scale:{1.f,1.5f,2.f})for(auto mode:{PenMode::Normal,PenMode::Highlighter}){
        for(Point end:std::array<Point,5>{Point{370,233},Point{370,140},Point{140,330},Point{75,90},Point{140,140}}){
            prepare(scale,mode);const Point start{140,140};
            app.PointerDown(view,start);app.PointerMove(view,{210,310},MK_LBUTTON);
            expect(app.draft_&&app.draft_->points==std::vector<Point>{start,{210,310}},"line preview replaces points, without Shift");
            app.PointerMove(view,{180,170},MK_LBUTTON);app.PointerUp(view,end);
            expect(app.document_.marks.size()==1,"line gesture commits one annotation");
            if(app.document_.marks.size()!=1)continue;
            const auto mark=app.document_.marks[0];
            expect(mark.pen_straight&&mark.a==start&&mark.b==end&&mark.points==(start==end?std::vector<Point>{start}:std::vector<Point>{start,end}),"release endpoint is exact for arbitrary, horizontal, vertical, reverse and zero-length lines");
            expect(mark.pen_mode==mode&&mark.width==(mode==PenMode::Normal?3.f:24.f)*scale,"line mode preserves independent brush style at every DPI");
            expect(s.pen_straight&&s.tool==Tool::Pen,"completed line keeps drawing mode and property selection");
            app.Command(7);expect(app.document_.marks.empty()&&!app.document_.CanUndo(),"line drawing is one undo step");
            app.Command(8);expect(app.document_.marks.size()==1&&app.document_.marks[0]==mark,"redo restores exact line geometry and mode");
            if(scale==1&&end==Point{370,233}){
                Renderer renderer;const auto image=renderer.Flatten(*app.frame_,app.document_,RECT{30,30,900,700});
                auto expected=mark;expected.pen_straight=false;expected.pen_smoothing=PenSmoothing::None;
                Document reference;reference.Add(expected);
                expect(image.pixels==renderer.Flatten(*app.frame_,reference,RECT{30,30,900,700}).pixels,"straight export matches an unsmoothed two-endpoint segment");
                const auto path=mode==PenMode::Normal?L"pen-line/visuals/normal.png":L"pen-line/visuals/highlighter.png";
                SavePng(image,path);expect(ReadPng(path).pixels==image.pixels,"PNG round trip preserves line pixels");
                expect(std::any_of(image.pixels.begin(),image.pixels.end(),[](auto pixel){return pixel!=0xffeef2f6;}),"export contains visible line pixels");
            }
        }
    }
    prepare(1,PenMode::Normal);click(83);expect(!s.pen_straight,"freehand button switches via actual pointer hit testing");
    app.PointerDown(view,{140,140});app.PointerMove(view,{210,310},MK_LBUTTON);app.PointerUp(view,{370,233});
    expect(app.document_.marks.size()==1&&app.document_.marks[0].points.size()>=3,"freehand retains intermediate trajectory");
    const auto original=app.document_.marks[0];
    click(84);expect(s.pen_straight&&app.document_.marks[0].points==std::vector<Point>{original.points.front(),original.points.back()},"line property straightens the selected stroke");
    app.Command(7);expect(app.document_.marks[0]==original,"undo restores the entire original freehand stroke");
    // Undo/redo intentionally clear document selection; inspect properties after reselection.
    app.document_.selected=0;app.SyncSelectedProperties();expect(!s.pen_straight,"reselecting the restored curve reports freehand mode");
    app.Command(8);app.document_.selected=0;app.SyncSelectedProperties();
    expect(s.pen_straight&&app.document_.marks[0].pen_straight,"redo and reselection restore selected line property");
    click(83);expect(!s.pen_straight,"switching back to freehand is available on an existing line");
    prepare(1,PenMode::Normal);app.PointerDown(view,{140,140});app.PointerUp(view,{1200,800});
    expect(app.document_.marks.size()==1&&app.document_.marks[0].b==Point{900,700},"straight endpoint remains clipped to capture bounds");
    for(float scale:{1.f,1.5f,2.f})for(int width:{480,800,1280})for(bool straight:{false,true}){
        ViewState state;state.selected=true;state.tool=Tool::Pen;state.pen_straight=straight;
        state.toolbar=PlaceToolbar({-400,100,-50,400},{-width,0,0,900},scale,1,Tool::Pen);
        const auto controls=ToolbarControls(state,Document{});
        for(int id:{83,84,57}){
            const auto it=std::find_if(controls.begin(),controls.end(),[&](const auto& c){return c.id==id;});
            expect(it!=controls.end(),"line controls and smoothing remain in adaptive toolbar");if(it==controls.end())continue;
            if(id==57){expect(it->enabled==!straight,"smoothing is disabled only for straight mode");continue;}
            const auto b=it->bounds;
            expect(it->enabled&&it->selected==((id==84)==straight)&&ui::HitTest(controls,{(b.left+b.right)/2,(b.top+b.bottom)/2})==id,"line buttons are visible, mutually selected and clickable at narrow/high-DPI negative-origin layouts");
        }
    }
    for(float rotation:{0.f,30.f,-75.f}){
        prepare(1,PenMode::Normal);Mark curve;curve.tool=Tool::Pen;curve.a={140,140};curve.b={370,233};
        curve.points={curve.a,{210,410},curve.b};curve.rotation=rotation;
        const auto old_center=MarkCenter(curve);
        const auto first=RotatePoint(curve.a,old_center,rotation),last=RotatePoint(curve.b,old_center,rotation);
        app.document_.marks={curve};app.document_.selected=0;app.SyncSelectedProperties();app.Command(84);
        const auto line=app.document_.marks[0];const auto center=MarkCenter(line);
        const auto a=RotatePoint(line.a,center,rotation),b=RotatePoint(line.b,center,rotation);
        expect(std::hypot(a.x-first.x,a.y-first.y)<.001f&&std::hypot(b.x-last.x,b.y-last.y)<.001f,"straightening a rotated curve preserves screen-space endpoints");
        expect(app.document_.HitTest({(a.x+b.x)/2,(a.y+b.y)/2})==0,"straightened line remains selectable along its visible segment");
        app.Command(7);expect(app.document_.marks[0]==curve,"undo conversion restores rotated freehand geometry");
    }
    for(bool dark:{false,true}){Renderer renderer;SavePng(renderer.Demo(true,dark,Tool::Pen),dark?L"pen-line/visuals/toolbar-dark.png":L"pen-line/visuals/toolbar-light.png");}
}
