static float RenderedCursorAxis(HCURSOR cursor){
    auto image=MakeFrame({0,0,256,256},0xffffffff);auto frozen=FrozenCursor::Copy(cursor,{128,128});frozen.Composite(image);
    double weight=0,xsum=0,ysum=0,xx=0,xy=0,yy=0;
    for(int y=0;y<256;++y)for(int x=0;x<256;++x){
        const auto pixel=image.pixels[static_cast<size_t>(y)*256+x];const double w=255-static_cast<double>(pixel&255);
        weight+=w;xsum+=w*x;ysum+=w*y;xx+=w*x*x;xy+=w*x*y;yy+=w*y*y;
    }
    if(weight<=0)return -1000;
    xx-=xsum*xsum/weight;xy-=xsum*ysum/weight;yy-=ysum*ysum/weight;
    return static_cast<float>(std::atan2(2*xy,xx-yy)*90/3.14159265358979323846);
}
static bool CursorAxisMatches(HCURSOR cursor,float degrees){
    if(!cursor)return false;
    const float measured=RenderedCursorAxis(cursor);if(measured<-180)return false;
    const float difference=std::remainder(measured-degrees,180.f);
    return std::abs(difference)<2.f;
}
static void ExactResizeGallery(){
    Renderer renderer;auto image=MakeFrame({0,0,640,840},0xfff5f7fa);
    // This offscreen test is not a DPI-aware app window. Match the custom cursor
    // canvas to the actual system cursor canvas, rather than mixing 96-DPI custom
    // pixels with a larger native system cursor from the desktop's DPI.
    UINT cursorDpi=96;ICONINFO native{};
    if(GetIconInfo(LoadCursorW(nullptr,IDC_SIZEWE),&native)){
        BITMAP dimensions{};if(GetObjectW(native.hbmColor?native.hbmColor:native.hbmMask,sizeof(dimensions),&dimensions))cursorDpi=static_cast<UINT>(MulDiv(dimensions.bmWidth,96,32));
        if(native.hbmColor)DeleteObject(native.hbmColor);if(native.hbmMask)DeleteObject(native.hbmMask);
    }
    const auto check=[](HRESULT hr){CheckWin32(SUCCEEDED(hr),"Exact resize cursor gallery");};
    ComPtr<IWICImagingFactory> wic;check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)));
    ComPtr<IWICBitmap> bitmap;check(wic->CreateBitmap(image.Width(),image.Height(),GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap));
    check(renderer.factory_->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&renderer.target_));
    const std::array<float,6> angles{0,15,30,45,60,-20};
    std::vector<std::pair<Mark,std::vector<EditHandle>>> boxes;
    renderer.target_->BeginDraw();renderer.target_->Clear(D2D1::ColorF(0xf5f7fa));
    for(size_t i=0;i<angles.size();++i){
        const float cx=160+static_cast<float>(i%2)*320,cy=150+static_cast<float>(i/2)*280;
        Mark m;m.tool=Tool::Rectangle;m.a={cx-80,cy-45};m.b={cx+80,cy+45};m.corner_radius=15;m.rotation=angles[i];
        const auto handles=EditHandles(m,EditPart::Whole,1);boxes.emplace_back(m,handles);
        renderer.Brush(0xff087eff);
        for(int side=0;side<4;++side){const auto a=handles[static_cast<size_t>(side)*2].point,b=handles[(static_cast<size_t>(side)*2+2)%8].point;
            renderer.target_->DrawLine({a.x,a.y},{b.x,b.y},renderer.brush_.Get(),1);}
        renderer.Brush(0xffdb9da2);
        const std::array<float,8> local{45,90,135,0,45,90,135,0};
        for(size_t id=0;id<8;++id){const auto p=handles[id].point;const float a=(local[id]+m.rotation)*3.14159265358979323846f/180;
            const float dx=std::cos(a)*15,dy=std::sin(a)*15;
            renderer.target_->DrawLine({p.x-dx,p.y-dy},{p.x+dx,p.y+dy},renderer.brush_.Get(),1);}
    }
    check(renderer.target_->EndDraw());check(bitmap->CopyPixels(nullptr,image.Width()*4,static_cast<UINT>(image.pixels.size()*4),reinterpret_cast<BYTE*>(image.pixels.data())));
    for(const auto& [mark,handles]:boxes){
        Document document;document.marks={mark};document.selected=0;ViewState state;state.selected=true;state.tool=Tool::Select;
        for(size_t id=0;id<8;++id){const auto p=handles[id].point;const auto cursor=ui::SelectionEditCursor(document,state,p,cursorDpi);
            auto frozen=FrozenCursor::Copy(cursor,{static_cast<LONG>(std::lround(p.x)),static_cast<LONG>(std::lround(p.y))});frozen.Composite(image);}
    }
    SavePng(image,L"rotation-cursor/exact-resize-gallery.png");
}
template<class Expect>
static void RotationCursorCases(Application& app,Application::View& view,Expect expect){
    struct RestoreCursor {HCURSOR original{GetCursor()};~RestoreCursor(){SetCursor(original);}}restore;
    const std::array<UINT,6> dpis{96,120,144,192,288,384};
    std::array<HCURSOR,6> cursors{};
    auto gallery=MakeFrame({0,0,1200,864},0xfff5f7fa);
    for(int y=0;y<gallery.Height();++y)for(int x=600;x<1200;++x)gallery.pixels[static_cast<size_t>(y)*1200+x]=0xff17202c;
    for(size_t i=0;i<dpis.size();++i){
        const UINT dpi=dpis[i];const auto cursor=ui::RotationCursor(dpi);cursors[i]=cursor;
        expect(cursor&&cursor!=LoadCursorW(nullptr,IDC_HAND)&&cursor!=LoadCursorW(nullptr,IDC_CROSS),"rotation uses a dedicated circular-arrow cursor, not a hand or fallback");
        ICONINFO icon{};const bool ok=GetIconInfo(cursor,&icon)!=FALSE;expect(ok,"rotation cursor exposes valid Win32 icon metadata");
        if(ok){BITMAP bitmap{};GetObjectW(icon.hbmColor,sizeof(bitmap),&bitmap);const int size=MulDiv(32,static_cast<int>(dpi),96);
            expect(!icon.fIcon&&bitmap.bmWidth==size&&bitmap.bmHeight==size&&icon.xHotspot==static_cast<DWORD>(size/2)&&icon.yHotspot==static_cast<DWORD>(size/2),"DPI-scaled cursor keeps its hotspot exactly at the rotation center");
            if(icon.hbmColor)DeleteObject(icon.hbmColor);if(icon.hbmMask)DeleteObject(icon.hbmMask);
        }
        for(int base:{0,600}){
            for(int col=0;col<4;++col){
                HCURSOR shown=cursor;
                if(col<3)shown=static_cast<HCURSOR>(CopyImage(LoadCursorW(nullptr,col==0?IDC_ARROW:(col==1?IDC_SIZEALL:IDC_SIZENWSE)),IMAGE_CURSOR,MulDiv(32,static_cast<int>(dpi),96),MulDiv(32,static_cast<int>(dpi),96),LR_COPYFROMRESOURCE));
                expect(shown!=nullptr,"system comparison cursor loads at the same DPI");
                if(!shown)continue;
                ICONINFO metadata{};POINT position{base+75+col*150,72+static_cast<LONG>(i)*144};
                if(GetIconInfo(shown,&metadata)){
                    const LONG half=MulDiv(16,static_cast<int>(dpi),96);
                    position.x+=static_cast<LONG>(metadata.xHotspot)-half;position.y+=static_cast<LONG>(metadata.yHotspot)-half;
                    if(metadata.hbmColor)DeleteObject(metadata.hbmColor);if(metadata.hbmMask)DeleteObject(metadata.hbmMask);
                }
                auto frozen=FrozenCursor::Copy(shown,position);frozen.Composite(gallery);
                if(col<3&&shown)DestroyCursor(shown);
            }
        }
    }
    const auto objects=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);bool cached=true;
    for(int n=0;n<2000;++n)for(size_t i=0;i<dpis.size();++i)cached=cached&&ui::RotationCursor(dpis[i])==cursors[i];
    expect(cached&&GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==objects,"repeated hover reuses cursor handles without allocating GDI objects");
    std::filesystem::create_directories("rotation-cursor");SavePng(gallery,L"rotation-cursor/dpi-gallery.png");
    expect(ReadPng(L"rotation-cursor/dpi-gallery.png").pixels==gallery.pixels,"actual cursor raster survives light/dark PNG preview round trip");
    for(float scale:{1.f,1.5f,2.f})for(float rotation:{0.f,35.f,-75.f})for(float origin:{0.f,-700.f}){
        Mark mark;mark.tool=Tool::Rectangle;mark.a={origin+100,150};mark.b={origin+340,300};mark.fill_color=0xffabcdef;mark.corner_radius=20;mark.rotation=rotation;
        Document document;document.marks={mark};document.selected=0;ViewState state;state.selected=true;state.tool=Tool::Select;state.toolbar.scale=scale;state.toolbar.bounds={900,700,1100,800};
        const UINT dpi=static_cast<UINT>(96*scale);const auto handles=EditHandles(mark,EditPart::Whole,scale);
        expect(ui::SelectionEditCursor(document,state,handles[8].point,dpi)==ui::RotationCursor(dpi),"rotation grip hover uses rotation cursor at rotated/negative-origin/DPI positions");
        expect(CursorAxisMatches(ui::SelectionEditCursor(document,state,handles[0].point,dpi),45+rotation),"corner resize cursor follows object rotation");
        expect(CursorAxisMatches(ui::SelectionEditCursor(document,state,handles[1].point,dpi),90+rotation),"edge resize cursor follows object rotation");
        expect(ui::SelectionEditCursor(document,state,MarkCenter(mark),dpi)==LoadCursorW(nullptr,IDC_SIZEALL),"object body still uses movement cursor");
        expect(ui::SelectionEditCursor(document,state,{1200,850},dpi,8)==ui::RotationCursor(dpi),"active rotation keeps its cursor away from the handle");
        state.toolbar.bounds={handles[8].point.x-10,handles[8].point.y-10,handles[8].point.x+10,handles[8].point.y+10};
        expect(!ui::SelectionEditCursor(document,state,handles[8].point,dpi),"toolbar controls take precedence over an underlying rotation grip");
        state.toolbar.bounds={900,700,1100,800};state.picker.open=true;
        expect(!ui::SelectionEditCursor(document,state,handles[8].point,dpi),"color picker does not inherit rotation cursor");
        state.picker.open=false;state.busy=true;expect(!ui::SelectionEditCursor(document,state,handles[8].point,dpi),"busy capture does not expose an edit cursor");
        state.busy=false;document.selected=-1;expect(!ui::SelectionEditCursor(document,state,handles[8].point,dpi),"no selected annotation has no rotation cursor");
    }
    const std::array<float,17> angles{0,15,30,22.49f,22.51f,35,45,60,67.49f,67.51f,90,135,180,395,-435,-45,-20};
    const std::array<float,8> localAxes{45,90,135,0,45,90,135,0};
    for(const float angle:angles)for(float scale:{1.f,1.25f,1.5f,2.f})for(float origin:{0.f,-700.f}){
        Mark rectangle;rectangle.tool=Tool::Rectangle;rectangle.a={origin+200,200};rectangle.b={origin+500,400};rectangle.rotation=angle;rectangle.corner_radius=20;
        Document document;document.marks={rectangle};document.selected=0;
        ViewState state;state.selected=true;state.tool=Tool::Select;state.toolbar.scale=scale;state.toolbar.bounds={900,700,1100,800};
        const UINT dpi=static_cast<UINT>(scale*96);const auto handles=EditHandles(rectangle,EditPart::Whole,scale);
        for(int id=0;id<8;++id){
            const auto cursor=ui::SelectionEditCursor(document,state,handles[static_cast<size_t>(id)].point,dpi);
            expect(CursorAxisMatches(cursor,localAxes[static_cast<size_t>(id)]+angle),"actual cursor raster follows the local axis plus exact rotation, NOT the nearest Windows sector");
            expect(ui::SelectionEditCursor(document,state,{1000,750},dpi,id)==cursor,"captured resize preserves the exact angle away from the grip");
        }
        expect(!ui::SelectionEditCursor(document,state,{1000,750},dpi),"toolbar retains precedence outside captured resizing");
        expect(ui::SelectionEditCursor(document,state,handles[9].point,dpi)==LoadCursorW(nullptr,IDC_CROSS),"rotated radius grips retain their cross cursor");
    }
    expect(ui::AngledResizeCursor(0,96)==LoadCursorW(nullptr,IDC_SIZEWE)&&ui::AngledResizeCursor(90,96)==LoadCursorW(nullptr,IDC_SIZENS),"exact standard axes retain the real system cursors");
    const auto activeCursor=ui::AngledResizeCursor(12.345f,96);SetCursor(activeCursor);
    const auto resources=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    for(int i=0;i<100;++i)(void)ui::AngledResizeCursor(.123f+static_cast<float>(i)*.713f,96);
    expect(GetCursor()==activeCursor&&CursorAxisMatches(activeCursor,12.345f),"bounded cursor cache never destroys the currently displayed cursor");
    expect(GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)<=resources+64,"arbitrary angles use a bounded cursor resource cache");
    SetCursor(restore.original);ExactResizeGallery();
    auto& state=app.state_;state=ViewState{};state.selected=true;state.tool=Tool::Select;state.selection={30,30,900,700};
    Mark mark;mark.tool=Tool::Rectangle;mark.a={200,200};mark.b={400,320};mark.fill_color=0xffabcdef;
    app.document_.Reset();app.document_.marks={mark};app.document_.selected=0;
    app.SyncSelectedProperties();state.toolbar=PlaceToolbar(state.selection,view.bounds,1,1,Tool::Rectangle);
    const auto grip=EditHandles(mark,EditPart::Whole,1)[8].point;
    const auto expected=ui::RotationCursor(GetDpiForWindow(view.window));
    app.PointerDown(view,grip);expect(app.mark_moving_&&app.mark_handle_==8&&GetCursor()==expected,"pressing rotation handle enters existing drag and installs the rotation cursor");
    app.PointerMove(view,{430,260},MK_LBUTTON);expect(GetCursor()==expected,"rotation cursor stays visible while dragging away from the handle");
    app.PointerUp(view,{430,260});const auto rotated=app.document_.marks[0];
    expect(std::abs(rotated.rotation-90)<.01f&&!app.mark_moving_&&app.mark_handle_==-1,"rotation geometry still follows the pointer and ends normally");
    expect(GetCursor()!=expected,"release away from the handle does not leave a stuck rotation cursor");
    app.Command(7);expect(app.document_.marks[0]==mark,"rotation remains a single undo step");app.Command(8);expect(app.document_.marks[0]==rotated,"redo restores the rotation");
    for(float angle:{15.f,30.f,35.f,45.f,90.f})for(int id=0;id<8;++id){
        state=ViewState{};state.selected=true;state.tool=Tool::Select;state.selection={30,30,900,700};
        Mark original;original.tool=Tool::Rectangle;original.a={200,200};original.b={500,400};original.fill_color=0xffabcdef;original.corner_radius=20;original.rotation=angle;
        app.document_.Reset();app.document_.marks={original};app.document_.selected=0;app.SyncSelectedProperties();
        state.toolbar=PlaceToolbar(state.selection,view.bounds,1,1,Tool::Rectangle);
        const auto start=EditHandles(original,EditPart::Whole,1)[static_cast<size_t>(id)].point;
        const Point finish{start.x+26,start.y+19};
        const auto cursor=ui::SelectionEditCursor(app.document_,state,start,GetDpiForWindow(view.window));
        expect(CursorAxisMatches(cursor,localAxes[static_cast<size_t>(id)]+angle),"actual interaction cursor is aligned before pointer down");
        app.PointerDown(view,start);
        expect(app.mark_moving_&&app.mark_handle_==id&&GetCursor()==cursor,"pointer down installs the rotated resize cursor");
        app.PointerMove(view,finish,MK_LBUTTON);
        expect(GetCursor()==cursor,"pointer movement keeps the active rotated resize cursor");
        app.PointerUp(view,finish);const auto resized=app.document_.marks[0];
        expect(resized!=original&&resized.rotation==angle&&!app.mark_moving_,"rotated resize still changes geometry without changing its rotation");
        const auto released=ui::SelectionEditCursor(app.document_,state,finish,GetDpiForWindow(view.window));
        expect(GetCursor()==(released?released:LoadCursorW(nullptr,IDC_ARROW)),"release recalculates the hover cursor instead of leaving a captured axis");
        app.Command(7);expect(app.document_.marks[0]==original,"rotated resize remains one undo step");
        app.Command(8);expect(app.document_.marks[0]==resized,"redo restores rotated resize geometry");
    }
}
