static Frame PolylineScene(Application& app){
    Renderer renderer;auto image=*app.frame_;const auto acrylic=BlurBackdrop(image);
    auto preview=app.state_;preview.toolbar=PlaceToolbar(preview.selection,image.bounds,1,1,Tool::Pen,preview.number_combo);
    const auto check=[](HRESULT hr){CheckWin32(SUCCEEDED(hr),"Synthetic polyline scene");};
    ComPtr<IWICImagingFactory> wic;check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)));
    ComPtr<IWICBitmap> bitmap;check(wic->CreateBitmap(image.Width(),image.Height(),GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap));
    check(renderer.factory_->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&renderer.target_));
    renderer.screen_bitmap_=renderer.Bitmap(image);renderer.acrylic_bitmap_=renderer.Bitmap(acrylic);
    renderer.target_->BeginDraw();renderer.Chrome(image,acrylic,app.document_,app.draft_,preview,image.bounds);check(renderer.target_->EndDraw());
    check(bitmap->CopyPixels(nullptr,image.Width()*4,static_cast<UINT>(image.pixels.size()*4),reinterpret_cast<BYTE*>(image.pixels.data())));
    return image;
}
template<class Expect>
static void SnapVectorPreview(Expect expect){
    Renderer renderer;auto image=MakeFrame({0,0,800,432},0xfff5f7fa);
    const auto check=[](HRESULT hr){CheckWin32(SUCCEEDED(hr),"Synthetic vector snap preview");};
    ComPtr<IWICImagingFactory> wic;check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)));
    ComPtr<IWICBitmap> bitmap;check(wic->CreateBitmap(image.Width(),image.Height(),GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap));
    check(renderer.factory_->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&renderer.target_));
    renderer.target_->BeginDraw();renderer.target_->Clear(D2D1::ColorF(0xf5f7fa));
    renderer.Brush(0xff17202c);renderer.target_->FillRectangle({400,0,800,432},renderer.brush_.Get());
    const std::array<float,6> scales{1,1.25f,1.5f,2,3,4};
    const std::array<LineSnapKind,5> kinds{LineSnapKind::Endpoint,LineSnapKind::Midpoint,LineSnapKind::Nearest,LineSnapKind::Horizontal,LineSnapKind::None};
    for(size_t row=0;row<scales.size();++row)for(int bg=0;bg<2;++bg)for(size_t col=0;col<kinds.size();++col){
        ViewState state;state.tool=Tool::Pen;state.pen_snap=true;state.toolbar.scale=scales[row];
        const Point p{40+static_cast<float>(col)*80+bg*400,36+static_cast<float>(row)*72};
        state.line_snap=LineSnap{p,p,kinds[col]};
        std::optional<Mark> draft;
        if(col==4){draft=Mark{};draft->points={p};state.polyline_active=true;state.polyline_confirmed=1;}
        renderer.target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        renderer.SnapGuide(state,draft);
        expect(renderer.target_->GetAntialiasMode()==D2D1_ANTIALIAS_MODE_ALIASED,"snap vectors restore the caller's antialias state");
    }
    check(renderer.target_->EndDraw());
    check(bitmap->CopyPixels(nullptr,image.Width()*4,static_cast<UINT>(image.pixels.size()*4),reinterpret_cast<BYTE*>(image.pixels.data())));
    bool colored=false,whiteHalo=false,partialCoverage=false;
    for(int y=0;y<image.Height();++y)for(int x=400;x<800;++x){
        const auto pixel=image.pixels[static_cast<size_t>(y)*800+x];
        if((pixel&0xffffff)==0x17202c)continue;
        const auto r=(pixel>>16)&255,g=(pixel>>8)&255,b=pixel&255;
        colored=true;whiteHalo=whiteHalo||(r>180&&g>180&&b>180);
        partialCoverage=partialCoverage||(g>40&&g<120&&r<30);
    }
    expect(colored&&!whiteHalo,"snap symbols and vertex dots have no white outline or halo on dark backgrounds");
    expect(partialCoverage,"vector snap edges contain smooth partial pixel coverage rather than aliased steps");
    std::filesystem::create_directories("polyline/visuals");SavePng(image,L"polyline/visuals/vector-snap-dpi.png");
}
template<class Expect>
static void PolylineCases(Application& app,Application::View& view,Expect expect){
    SnapVectorPreview(expect);
    const Box bounds{-1000,-800,1000,1000};
    const auto samePoint=[](Point a,Point b){return std::hypot(a.x-b.x,a.y-b.y)<.001f;};
    Document geometry;Mark edge;edge.tool=Tool::Pen;edge.pen_straight=true;edge.a={-300,-200};edge.b={-100,-200};edge.points={edge.a,edge.b};geometry.Add(edge);
    for(float scale:{1.f,1.5f,2.f}){
        const auto snap=SnapLinePoint({edge.a.x+6*scale,edge.a.y},std::nullopt,geometry,{},bounds,scale,true);
        expect(snap.kind==LineSnapKind::Endpoint&&snap.point==edge.a,"endpoint radius scales with DPI at negative screen coordinates");
        expect(SnapLinePoint({edge.a.x-8*scale-1,edge.a.y},std::nullopt,geometry,{},bounds,scale,true).kind==LineSnapKind::None,"outside magnetic radius stays free");
    }
    geometry.Reset();
    const auto horizontal=SnapLinePoint({300,104},Point{100,100},geometry,{},bounds,1,true);
    const auto vertical=SnapLinePoint({104,300},Point{100,100},geometry,{},bounds,1,true);
    const auto diagonal=SnapLinePoint({298,302},Point{100,100},geometry,{},bounds,1,true);
    expect(horizontal.kind==LineSnapKind::Horizontal&&horizontal.point==Point{300,100},"horizontal direction snap");
    expect(vertical.kind==LineSnapKind::Vertical&&vertical.point==Point{100,300},"vertical direction snap");
    expect(diagonal.kind==LineSnapKind::Diagonal&&samePoint(diagonal.point,Point{300,300}),"45 degree direction snap");
    expect(SnapLinePoint({280,210},Point{100,100},geometry,{},bounds,1,true).kind==LineSnapKind::None,"arbitrary directions are not forced to 45 degrees");
    expect(SnapLinePoint({106,103},Point{100,100},geometry,{},bounds,1,true).kind==LineSnapKind::None,"short segments do not jump to a direction");
    edge.a={202,105};edge.b={400,300};edge.points={edge.a,edge.b};geometry.Add(edge);
    expect(SnapLinePoint({203,102},Point{100,100},geometry,{},bounds,1,true).point==edge.a,"nearby endpoint takes precedence over closer direction guide");
    expect(SnapLinePoint({203,102},Point{100,100},geometry,{},bounds,1,false).point==Point{203,102},"snap off keeps exact pointer coordinates");
    auto rotated=edge;rotated.rotation=30;geometry.marks={rotated};const auto endpoint=RotatePoint(rotated.a,MarkCenter(rotated),30);
    expect(samePoint(SnapLinePoint({endpoint.x+2,endpoint.y+2},std::nullopt,geometry,{},bounds,1,true).point,endpoint),"rotated annotations expose world-space endpoints");
    const std::vector<Point> vertices{{100,100},{200,100},{200,200}};
    expect(SnapLinePoint({103,102},vertices.back(),Document{},vertices,bounds,1,true).point==vertices.front(),"current polyline can snap back to its first vertex");
    const auto boundary=SnapLinePoint({997,1000},Point{980,995},Document{}, {},bounds,1,true);
    expect(Contains(bounds,boundary.point),"direction snapping never leaves the capture bounds");

    // Geometric snaps: midpoint wins over a nearer projection; endpoints win over both.
    geometry.Reset();edge.rotation=0;edge.a={100,100};edge.b={300,100};edge.points={edge.a,edge.b};geometry.marks={edge};
    for(float scale:{1.f,1.5f,2.f}){
        const auto mid=SnapLinePoint({203,100+4*scale},std::nullopt,geometry,{},bounds,scale,true);
        expect(mid.kind==LineSnapKind::Midpoint&&mid.point==Point{200,100},"midpoint has priority over nearest projection at every DPI");
        const auto projected=SnapLinePoint({250,100+4*scale},std::nullopt,geometry,{},bounds,scale,true);
        expect(projected.kind==LineSnapKind::Nearest&&projected.point==Point{250,100},"nearest point projects onto the finite line segment");
        expect(SnapLinePoint({250,100+9*scale},std::nullopt,geometry,{},bounds,scale,true).kind==LineSnapKind::None,"nearest point respects the DPI-scaled magnetic radius");
    }
    expect(SnapLinePoint({320,100},std::nullopt,geometry,{},bounds,1,true).kind==LineSnapKind::None,"nearest point never extends beyond the finite segment");
    expect(SnapLinePoint({104,101},std::nullopt,geometry,{},bounds,1,true).kind==LineSnapKind::Endpoint,"endpoint wins over nearest projection");
    expect(SnapLinePoint({203,102},std::nullopt,geometry,{},bounds,1,false).point==Point{203,102},"snap off disables midpoints and projections too");
    for(auto tool:{Tool::Pen,Tool::Arrow}){
        auto line=edge;line.tool=tool;line.rotation=37;geometry.marks={line};
        const auto center=MarkCenter(line);const auto mid=RotatePoint({200,100},center,line.rotation);
        const auto foot=RotatePoint({250,100},center,line.rotation);const auto probe=RotatePoint({250,104},center,line.rotation);
        expect(samePoint(SnapLinePoint({mid.x+2,mid.y+2},std::nullopt,geometry,{},bounds,1,true).point,mid),"rotated line and arrow midpoint uses world coordinates");
        const auto hit=SnapLinePoint(probe,std::nullopt,geometry,{},bounds,1,true);
        expect(hit.kind==LineSnapKind::Nearest&&samePoint(hit.point,foot),"rotated line and arrow nearest point uses world coordinates");
    }
    auto chain=edge;chain.pen_polyline=true;chain.points={{-300,-300},{-100,-300},{-100,-100}};chain.a=chain.points.front();chain.b=chain.points.back();geometry.marks={chain};
    expect(SnapLinePoint({-98,-203},std::nullopt,geometry,{},bounds,1,true).point==Point{-100,-200},"each polyline segment exposes its midpoint at negative coordinates");
    expect(SnapLinePoint({-96,-150},std::nullopt,geometry,{},bounds,1,true).point==Point{-100,-150},"each polyline segment exposes its nearest point");
    expect(SnapLinePoint({-98,-203},chain.points.back(),Document{},chain.points,bounds,1,true).kind==LineSnapKind::Midpoint,"confirmed active segments also expose midpoints");
    auto freehand=edge;freehand.pen_straight=false;freehand.points={{100,100},{200,180},{300,100}};geometry.marks={freehand};
    expect(SnapLinePoint({150,140},std::nullopt,geometry,{},bounds,1,true).kind==LineSnapKind::None,"freehand sample segments are not treated as straight snap geometry");
    auto zero=edge;zero.b=zero.a;zero.points={zero.a,zero.a};geometry.marks={zero};
    for(auto type:{ArrowType::Curved,ArrowType::HandDrawn}){
        auto curved=edge;curved.tool=Tool::Arrow;curved.arrow_type=type;geometry.marks={curved};
        expect(SnapLinePoint({200,100},std::nullopt,geometry,{},bounds,1,true).kind==LineSnapKind::None,"curved arrows never expose an invisible straight chord as a snap target");
    }
    geometry.marks={zero};
    const auto degenerate=SnapLinePoint({120,103},std::nullopt,geometry,{},bounds,1,true);
    expect(degenerate.kind==LineSnapKind::None&&std::isfinite(degenerate.point.x),"zero-length segments do not produce invalid projections");

    auto& s=app.state_;
    const auto prepare=[&](float scale=1.f,PenMode mode=PenMode::Normal){
        app.EndPolyline(true);app.document_.Reset();app.draft_.reset();app.pin_edit_id_=0;
        s=ViewState{};s.selected=true;s.selection={30,30,900,600};
        app.Command(static_cast<int>(Tool::Pen));app.Command(85);app.Command(mode==PenMode::Highlighter?59:58);
        app.toolbar_transition_.Reset(1);app.UpdateToolbar({150,150});s.toolbar.scale=scale;
    };
    const auto click=[&](Point p){app.PointerDown(view,p);app.PointerUp(view,p);};
    const auto control=[&](int id){const auto b=s.toolbar.Property(id,Tool::Pen);click({(b.left+b.right)/2,(b.top+b.bottom)/2});};
    for(float scale:{1.f,1.5f,2.f})for(auto mode:{PenMode::Normal,PenMode::Highlighter}){
        prepare(scale,mode);s.pen_smoothing=PenSmoothing::High;
        click({140,140});expect(app.line_vertices_.size()==1&&app.document_.marks.empty()&&s.polyline_active,"mouse release keeps the click-chain active without adding a mark");
        app.PointerMove(view,{300,144},0);
        expect(app.draft_&&app.draft_->points==std::vector<Point>{{140,140},{300,140}}&&s.line_snap&&s.line_snap->kind==LineSnapKind::Horizontal,"hover previews a snapped segment without holding a button");
        click({300,144});click({304,300});app.PointerMove(view,{490,410},0);
        expect(app.line_vertices_==std::vector<Point>{{140,140},{300,140},{300,300}},"clicks append snapped vertices without committing hover ghost");
        app.Key(view,VK_RETURN);
        expect(app.frame_&&app.active_&&!app.pending_&&!s.busy&&app.line_vertices_.empty(),"Enter finishes polyline only, without exporting or closing screenshot");
        expect(app.document_.marks.size()==1,"completed chain is one annotation");
        if(app.document_.marks.size()!=1)continue;
        const auto mark=app.document_.marks[0];
        expect(mark.pen_polyline&&mark.pen_straight&&mark.points==std::vector<Point>{{140,140},{300,140},{300,300}},"Enter drops the unconfirmed preview endpoint");
        expect(mark.pen_mode==mode&&mark.width==(mode==PenMode::Normal?3.f:24.f)*scale,"polyline retains normal/highlighter styles at each DPI");
        Renderer renderer;const auto image=renderer.Flatten(*app.frame_,app.document_,RECT{30,30,900,600});
        Document reference;auto exact=mark;exact.pen_polyline=false;exact.pen_straight=false;exact.pen_smoothing=PenSmoothing::None;reference.Add(exact);
        expect(image.pixels==renderer.Flatten(*app.frame_,reference,RECT{30,30,900,600}).pixels,"polyline corners remain straight even with high freehand smoothing");
        app.Command(7);expect(app.document_.marks.empty()&&!app.document_.CanUndo(),"completed polyline is a single undo step");
        app.Command(8);expect(app.document_.marks.size()==1&&app.document_.marks[0]==mark,"redo restores all vertices and drawing mode");
    }
    prepare();control(84);expect(!s.pen_polyline&&s.pen_straight,"existing drag-line mode remains directly available");control(85);expect(s.pen_polyline,"continuous-line button uses the real property hit target");
    click({140,140});click({300,140});click({300,300});
    app.Key(view,VK_BACK);expect(app.line_vertices_.size()==2&&app.document_.marks.empty(),"Backspace retracts just the last vertex");
    app.Command(7);expect(app.line_vertices_.size()==1,"toolbar undo retracts a vertex instead of document history");
    const auto controls=ToolbarControls(s,app.document_);const auto undo=std::find_if(controls.begin(),controls.end(),[](const auto& c){return c.id==7;});
    expect(undo!=controls.end()&&undo->enabled,"toolbar undo is enabled even before the first polyline is committed");
    app.Command(7);expect(app.line_vertices_.empty()&&!app.draft_&&!s.polyline_active,"retracting the first point clears the draft cleanly");
    click({140,140});click({300,140});app.Key(view,VK_ESCAPE);
    expect(app.line_vertices_.empty()&&app.document_.marks.empty()&&app.frame_&&app.active_,"Esc cancels this chain without closing capture");
    click({140,140});click({300,140});app.PointerMove(view,{400,300},0);app.PointerDoubleClick(view,{400,300});app.PointerUp(view,{400,300});
    expect(app.document_.marks.size()==1&&app.document_.marks[0].points.size()==3&&!app.pending_,"double click finishes without duplicate vertices or screenshot copy");
    prepare();click({140,140});click({300,140});click({300,300});click({143,142});
    expect(app.line_vertices_.empty()&&app.document_.marks.size()==1&&app.document_.marks[0].points.front()==app.document_.marks[0].points.back(),"snapping back to the start closes a polygon");
    prepare();click({140,140});click({300,140});click({300,300});app.Command(84);
    expect(app.document_.marks.size()==1&&app.document_.marks[0].points.size()==3&&app.document_.marks[0].pen_polyline&&!s.pen_polyline,"switching mode commits confirmed vertices without flattening the finished chain");
    prepare();click({140,140});click({300,140});click({300,300});app.Key(view,VK_RETURN);
    const auto finished=app.document_.marks[0];app.Command(84);
    expect(app.document_.marks[0]==finished&&!s.pen_polyline,"choosing drag-line after Enter does not collapse the previously completed polyline");
    app.Command(0);click({300,260});app.PointerDown(view,{300,260});app.PointerMove(view,{350,290},MK_LBUTTON);app.PointerUp(view,{350,290});
    auto translated=finished;Translate(translated,{50,30});
    expect(app.document_.marks[0]==translated,"selection drag moves all polyline vertices together");
    app.Command(7);expect(app.document_.marks[0]==finished,"undo restores the whole moved polyline");
    app.document_.selected=0;app.SyncSelectedProperties();app.Command(59);
    expect(app.document_.marks[0].pen_mode==PenMode::Highlighter&&app.document_.marks[0].points==finished.points,"selected polyline style editing preserves every vertex");
    prepare();click({140,140});click({300,140});app.Command(1);
    expect(app.document_.marks.size()==1&&s.tool==Tool::Rectangle&&!s.polyline_active,"switching tools commits the chain and clears its interaction state");
    prepare();click({140,140});app.PointerMove(view,{300,144},0);app.Command(86);
    expect(!s.pen_snap&&!s.line_snap&&app.draft_->b==Point{300,144},"snap toggle immediately updates the ghost to the unsnapped pointer");
    click({300,144});app.Key(view,VK_RETURN);expect(app.document_.marks[0].b==Point{300,144},"snap-off commits the exact clicked point");
    prepare();app.Command(84);edge.rotation=0;edge.a={100,100};edge.b={300,200};edge.points={edge.a,edge.b};app.document_.marks={edge};
    app.PointerDown(view,{103,103});app.PointerMove(view,{450,270},MK_LBUTTON);app.PointerUp(view,{450,270});
    expect(app.document_.marks.size()==2&&app.document_.marks[1].a==Point{100,100}&&app.document_.marks[0]==edge&&s.tool==Tool::Pen,"dragging from a snapped existing endpoint draws instead of moving the old annotation");
    app.PointerDown(view,{450,450});app.PointerUp(view,{302,202});
    expect(app.document_.marks.size()==3&&app.document_.marks[2].b==Point{300,200},"drag-line release snaps to an existing endpoint");
    prepare();app.pin_edit_id_=77;click({140,140});click({300,140});app.Key(view,VK_RETURN);
    expect(app.pin_edit_id_==77&&app.document_.marks.size()==1&&!app.pending_,"pinned-image edit sessions share the same non-exporting completion path");app.pin_edit_id_=0;
    prepare();edge.rotation=0;edge.a={500,100};edge.b={700,100};edge.points={edge.a,edge.b};app.document_.marks={edge};
    click({603,103});expect(app.line_vertices_.front()==Point{600,100},"continuous drawing can start from an existing midpoint");
    click({650,104});expect(app.line_vertices_.back()==Point{650,100},"continuous click commits a nearest point");
    expect(s.polyline_confirmed==app.line_vertices_.size(),"vertex overlay tracks confirmed clicks");
    app.Key(view,VK_BACK);expect(s.polyline_confirmed==1,"vertex overlay removes an undone vertex");
    app.Key(view,VK_ESCAPE);expect(s.polyline_confirmed==0,"cancel clears vertex overlay state");
    SetWindowLongPtrW(view.window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&view));
    prepare();click({140,140});click({300,140});app.PointerMove(view,{400,300},0);
    Application::OverlayProc(view.window,WM_RBUTTONUP,0,0);
    expect(app.document_.marks.size()==1&&app.document_.marks[0].points.size()==2&&app.active_,"right click keeps confirmed segments but not the rubber-band preview");
    Application::OverlayProc(view.window,WM_KEYDOWN,VK_RETURN,LPARAM(1)<<30);
    expect(app.active_&&!app.pending_&&!s.busy,"held Enter cannot immediately export after completing a line");
    SetWindowLongPtrW(view.window,GWLP_USERDATA,0);

    for(float scale:{1.f,1.5f,2.f})for(int width:{480,800,1280}){
        ViewState state;state.selected=true;state.tool=Tool::Pen;state.pen_straight=true;state.pen_polyline=true;
        state.toolbar=PlaceToolbar({-400,100,-50,400},{-width,0,0,900},scale,1,Tool::Pen);
        const auto items=ToolbarControls(state,Document{});
        for(int id:{83,84,85,86}){const auto it=std::find_if(items.begin(),items.end(),[&](const auto& c){return c.id==id;});
            expect(it!=items.end()&&it->enabled,"all drawing and snap buttons fit adaptive DPI/narrow-screen layouts");
            if(it!=items.end()){const auto b=it->bounds;expect(ui::HitTest(items,{(b.left+b.right)/2,(b.top+b.bottom)/2})==id,"continuous-line and snap controls hit their visible bounds");}
        }
    }
    std::filesystem::create_directories("polyline/visuals");
    for(bool dark:{false,true}){
        prepare();s.dark=dark;edge.a={600,100};edge.b={600,350};edge.points={edge.a,edge.b};app.document_.marks={edge};
        click({140,140});click({400,140});click({400,350});app.PointerMove(view,{603,352},0);
        expect(s.line_snap&&s.line_snap->kind==LineSnapKind::Endpoint,"endpoint marker appears in the synthetic preview");
        const auto withGuide=PolylineScene(app);const auto feedback=s.line_snap;s.line_snap.reset();const auto withoutGuide=PolylineScene(app);s.line_snap=feedback;
        expect(withGuide.pixels!=withoutGuide.pixels,"capture chrome renders the snap glyph without tooltip text");
        SavePng(withGuide,dark?L"polyline/visuals/dark.png":L"polyline/visuals/light.png");
        for(const auto kind:{LineSnapKind::Midpoint,LineSnapKind::Nearest}){
            app.PointerMove(view,kind==LineSnapKind::Midpoint?Point{604,225}:Point{604,290},0);
            expect(s.line_snap&&s.line_snap->kind==kind,"live hover exposes midpoint and nearest-point feedback");
            const auto marked=PolylineScene(app);const auto saved=s.line_snap;s.line_snap.reset();const auto plain=PolylineScene(app);s.line_snap=saved;
            bool localOnly=true,changed=false;
            for(int y=0;y<marked.Height();++y)for(int x=0;x<marked.Width();++x){
                const auto at=static_cast<size_t>(y)*marked.Width()+x;
                if(marked.pixels[at]!=plain.pixels[at]){changed=true;if(!saved||std::abs(x-saved->point.x)>8||std::abs(y-saved->point.y)>8)localOnly=false;}
            }
            expect(changed&&localOnly,"object snap feedback is a small local symbol with no floating text label");
            SavePng(marked,kind==LineSnapKind::Midpoint?(dark?L"polyline/visuals/mid-dark.png":L"polyline/visuals/mid-light.png"):(dark?L"polyline/visuals/nearest-dark.png":L"polyline/visuals/nearest-light.png"));
        }
        click({603,352});app.Key(view,VK_RETURN);Renderer renderer;const auto flat=renderer.Flatten(*app.frame_,app.document_,RECT{30,30,900,600});
        SavePng(flat,L"polyline/visuals/export.png");expect(ReadPng(L"polyline/visuals/export.png").pixels==flat.pixels,"polyline PNG export round trip is pixel-exact");
    }
}
