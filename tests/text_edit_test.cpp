#include "app/application.h"
#include "export/png.h"
#include "model/note_color.h"
#include "ui/selection_render.h"
#include "ui/fonts.h"
#include "model/number_label.h"
#include "number_reconnect_cases.h"
#include <string_view>
#include <commctrl.h>
#include <algorithm>
#include <iostream>
#include <filesystem>

namespace lumashot {
struct TextEditTest {
    static void Pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
    static void Key(HWND window,WPARAM key,bool control=false,bool shift=false){
        BYTE saved[256]{};GetKeyboardState(saved);BYTE pressed[256]{};std::copy_n(saved,256,pressed);
        pressed[VK_CONTROL]=control?0x80:0;pressed[VK_SHIFT]=shift?0x80:0;SetKeyboardState(pressed);
        SendMessageW(window,WM_KEYDOWN,key,0);if(key==VK_RETURN&&shift&&IsWindow(window))SendMessageW(window,WM_CHAR,VK_RETURN,0);SetKeyboardState(saved);
    }
    static Frame Preview(Application& app){
        auto image=app.edit_frame_->Snapshot();const RECT area=app.edit_frame_->TextRect();
        DibSurface text(area.right-area.left,area.bottom-area.top);PrintWindow(app.edit_host_,text.Dc(),0);
        for(int y=0;y<area.bottom-area.top;++y)for(int x=0;x<area.right-area.left;++x)
            image.pixels[size_t(y+area.top)*image.Width()+x+area.left]=text.Pixels()[size_t(y)*(area.right-area.left)+x]|0xff000000;
        const uint32_t background=app.state_.dark?0xff151b23u:0xffffffffu;
        for(auto& pixel:image.pixels){const unsigned alpha=pixel>>24;uint32_t result=0xff000000;for(int shift:{0,8,16})result|=std::min(255u,((pixel>>shift)&255)+(((background>>shift)&255)*(255-alpha)+127)/255)<<shift;pixel=result;}
        return image;
    }
    #include "number_label_editor_cases.h"
    #include "tag_editor_cases.h"
    static int Run(bool numberLabelOnly=false){
        std::cout<<std::unitbuf;
        int failures=0;const auto expect=[&](bool value,const char* label){std::cout<<(value?"PASS ":"FAIL ")<<label<<'\n';if(!value)++failures;};
        Application app;const RECT bounds{40,40,940,640};app.monitors_={bounds};
        app.frame_=std::make_shared<Frame>(MakeFrame(bounds,0xffeef2f7));app.acrylic_=app.frame_;
        WNDCLASSW wc{};wc.lpfnWndProc=Application::OverlayProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"LumaShot.TextTestOverlay";RegisterClassW(&wc);
        auto view=std::make_unique<Application::View>();view->app=&app;view->bounds=bounds;view->renderer=std::make_unique<Renderer>();
        view->window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,wc.lpszClassName,L"Synthetic text editing fixture",WS_POPUP,40,40,900,600,nullptr,nullptr,wc.hInstance,view.get());
        if(!view->window)return 2;
        auto& v=*view;app.views_.push_back(std::move(view));
        auto& state=app.state_;state.selected=true;state.selection={80,80,850,530};state.tool=Tool::Text;
        state.toolbar=PlaceToolbar(state.selection,bounds,1,1,Tool::Text);
        ShowWindow(v.window,SW_SHOW);SetForegroundWindow(v.window);UpdateWindow(v.window);Pump();
        if(numberLabelOnly){LabelEditorCases(app,v,expect);return failures?1:0;}
        state.color=0xff243142;app.BeginText(v,{140,150});Pump();
        expect(app.edit_&&app.edit_host_&&app.edit_frame_&&GetAncestor(app.edit_,GA_ROOT)==app.edit_host_&&GetWindow(app.edit_host_,GW_OWNER)==app.edit_frame_->Window()&&GetWindow(app.edit_frame_->Window(),GW_OWNER)==v.window,"native editor and rounded frame have separate composition surfaces above the real DXGI overlay");
        GUITHREADINFO gui{sizeof(gui)};GetGUIThreadInfo(GetCurrentThreadId(),&gui);
        expect(gui.hwndFocus==app.edit_&&gui.hwndCaret==app.edit_&&gui.rcCaret.bottom>gui.rcCaret.top,"empty editor immediately owns a native insertion caret and keyboard focus");
        RECT initial{};GetWindowRect(app.edit_,&initial);
        for(wchar_t ch:std::wstring(L"可再次编辑的文字"))SendMessageW(app.edit_,WM_CHAR,ch,0);
        Key(app.edit_,VK_RETURN,false,true);
        for(wchar_t ch:std::wstring(L"第二行，也能实时看见"))SendMessageW(app.edit_,WM_CHAR,ch,0);
        Pump();RECT grown{};GetWindowRect(app.edit_,&grown);
        expect(app.edit_&&SendMessageW(app.edit_,EM_GETLINECOUNT,0,0)>=2&&grown.bottom>initial.bottom,"typed Chinese text and Shift+Enter grow the multiline editor without committing");
        for(int i=0;i<4;++i){v.renderer->FrameReady();InvalidateRect(v.window,nullptr,FALSE);UpdateWindow(v.window);Pump();}
        const POINT hit{grown.left+8,grown.top+8};expect(WindowFromPoint(hit)==app.edit_,"editor remains above the canvas after repeated flip-model presentations");
        GetGUIThreadInfo(GetCurrentThreadId(),&gui);
        std::cout<<"Native caret after input: "<<gui.rcCaret.left<<","<<gui.rcCaret.top<<" focus="<<(gui.hwndFocus==app.edit_)<<" owner="<<(gui.hwndCaret==app.edit_)<<'\n';
        expect(gui.hwndCaret==app.edit_&&gui.hwndFocus==app.edit_&&gui.rcCaret.left>0,"canvas repaint preserves text focus and the moving insertion caret");
        RECT host{};GetWindowRect(app.edit_host_,&host);DibSurface surface(host.right-host.left,host.bottom-host.top);
        expect(PrintWindow(app.edit_host_,surface.Dc(),0)!=FALSE,"native editing surface can be rendered for visual inspection");
        auto preview=MakeFrame({0,0,host.right-host.left,host.bottom-host.top});std::copy_n(surface.Pixels(),preview.pixels.size(),preview.pixels.begin());for(auto& pixel:preview.pixels)pixel|=0xff000000;
        size_t ink=0;for(auto pixel:preview.pixels)if(NoteLuminance(pixel)<.5)++ink;
        expect(ink>100,"actual native editor painting contains visible text pixels");
        expect(app.edit_text_glyphs_>0,"live input and PrintWindow rasterize glyphs through LumaText");
        SendMessageW(app.edit_,EM_SETSEL,0,3);const auto selected_preview=Preview(app);
        size_t selected_pixels=0;for(auto pixel:selected_preview.pixels)if((pixel&0xffffff)==0x267aff)++selected_pixels;
        expect(selected_pixels>20,"native selection has a visible LumaText selection backdrop");
        SavePng(selected_preview,L"text-editor-lumatext-selection.png");
        SendMessageW(app.edit_,EM_SETSEL,3,1);DWORD reverse_first{},reverse_last{};
        SendMessageW(app.edit_,EM_GETSEL,reinterpret_cast<WPARAM>(&reverse_first),reinterpret_cast<LPARAM>(&reverse_last));
        GetGUIThreadInfo(GetCurrentThreadId(),&gui);const auto active_position=SendMessageW(app.edit_,EM_POSFROMCHAR,1,0);
        expect(reverse_first==1&&reverse_last==3&&gui.rcCaret.left==static_cast<short>(LOWORD(active_position))&&gui.rcCaret.top==static_cast<short>(HIWORD(active_position)),"reverse selection retains its native active-end caret after LumaText repaint");
        Key(app.edit_,VK_LEFT);SendMessageW(app.edit_,EM_GETSEL,reinterpret_cast<WPARAM>(&reverse_first),reinterpret_cast<LPARAM>(&reverse_last));
        GetGUIThreadInfo(GetCurrentThreadId(),&gui);
        expect(reverse_first==1&&reverse_last==1&&gui.rcCaret.left==static_cast<short>(LOWORD(active_position)),"Left collapses reverse selection at the native leading caret");
        SendMessageW(app.edit_,EM_SETSEL,static_cast<WPARAM>(-1),-1);
        const auto frame_pixels=app.edit_frame_->Snapshot();expect((frame_pixels.pixels.front()>>24)==0,"rounded frame and shadow have transparent outer corners");
        Key(app.edit_,VK_RETURN);expect(!app.edit_&&!app.edit_host_&&!app.edit_frame_&&app.document_.marks.size()==1&&app.document_.marks[0].text.find(L"\r\n")!=std::wstring::npos,"Enter commits multiline content and destroys both editing surfaces");
        expect(app.document_.marks[0].a==Point{140,150}&&app.document_.marks[0].b.x<440&&app.document_.marks[0].text_auto_size,"new text bounds fit its content without including editor padding or the return button");
        expect(app.document_.marks[0].text_background==TextBackground::None,"temporary editing background is excluded from the annotation");
        app.document_.marks[0].rotation=25;app.document_.marks[0].text_auto_size=false;const auto original=app.document_.marks[0];app.BeginText(v,original.a,0);Pump();
        DWORD start{},end{};SendMessageW(app.edit_,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
        expect(start==0&&end==original.text.size(),"reopening a text object selects its existing content");
        SetWindowTextW(app.edit_,L"取消的文字");Key(app.edit_,VK_ESCAPE);expect(app.document_.marks[0]==original&&!app.edit_,"Escape cancels edits and preserves rotated object geometry and style");
        app.BeginText(v,original.a,0);SetWindowTextW(app.edit_,L"继续编辑");Key(app.edit_,VK_RETURN);
        expect(app.document_.marks[0].text==L"继续编辑"&&app.document_.marks[0].a==original.a&&app.document_.marks[0].b==original.b&&app.document_.marks[0].rotation==25,"reediting commits content without resetting rotation or dimensions");
        app.BeginText(v,original.a,0);SetWindowTextW(app.edit_,L"");Key(app.edit_,VK_RETURN);expect(app.document_.marks.empty(),"committing an emptied text object removes it");
        app.document_.Undo();expect(app.document_.marks.size()==1&&app.document_.marks[0].text==L"继续编辑","undo restores cleared text");
        for(bool dark:{false,true})for(uint32_t color:{0xff000000u,0xffffffffu,0xffffff00u,0xffff574fu}){
            state.dark=dark;state.color=color;state.toolbar.scale=1.5f;app.BeginText(v,{150,150});
            expect(NoteContrast(NoteLuminance(app.edit_display_color_),NoteLuminance(app.edit_surface_color_))>=3&&app.edit_surface_color_==TextEditorFrame::Background(dark)&&app.edit_color_==color,"preview ink remains readable while theme background and exported custom color stay exact at 150% scale");app.CommitText(true);
        }
        state.color=0xff243142;app.BeginText(v,{150,150});
        SendMessageW(app.edit_,WM_IME_STARTCOMPOSITION,0,0);Key(app.edit_,VK_ESCAPE);expect(app.edit_!=nullptr,"IME composition consumes Escape without cancelling the text object");
        app.edit_ime_text_=L"中文预编辑";app.edit_ime_first_=app.edit_ime_last_=0;
        const auto before_ime=app.edit_text_glyphs_;SavePng(Preview(app),L"text-editor-lumatext-ime.png");
        expect(app.edit_text_glyphs_>before_ime&&GetWindowTextLengthW(app.edit_)==0,"synthetic IME preedit is rendered with LumaText without committing native text");
        SendMessageW(app.edit_,WM_IME_ENDCOMPOSITION,0,0);SetWindowTextW(app.edit_,L"失焦提交");SetFocus(v.window);Pump();
        expect(!app.edit_&&app.document_.marks.back().text==L"失焦提交","focus loss commits text after the native notification completes");
        app.document_.Reset();Mark label;label.tool=Tool::Number;label.number_combo=NumberCombo::Text;label.a={150,150};label.b={182,182};label.number_target={440,210};label.number_text_preset=-2;label.text=L"原说明";app.document_.Add(label);
        const auto appearance=ResolveNoteAppearance(*app.frame_,label);app.BeginText(v,label.a,0);
        expect(app.edit_color_==appearance.ink&&!app.edit_bold_,"number note editing uses its resolved ink and the same regular weight as rendering");
        SetWindowTextW(app.edit_,L"修改说明");Key(app.edit_,VK_RETURN);
        expect(app.document_.marks[0].number_text_preset==-2&&app.document_.marks[0].text==L"修改说明","editing a badge label preserves automatic color mode");
        for(bool dark:{false,true}){
            state.dark=dark;state.toolbar.scale=1.5f;state.color=0xffff574f;state.font_size=24;app.BeginText(v,{150,150});SetWindowTextW(app.edit_,L"水电费是的啊");const int length=GetWindowTextLengthW(app.edit_);SendMessageW(app.edit_,EM_SETSEL,length,length);SendMessageW(app.edit_,EM_SCROLLCARET,0,0);Pump();
            SavePng(Preview(app),dark?L"docs/text-editor-dark-preview.png":L"docs/text-editor-preview.png");
            const RECT button=app.edit_frame_->ButtonRect();const LPARAM point=MAKELPARAM((button.left+button.right)/2,(button.top+button.bottom)/2);const HWND frame=app.edit_frame_->Window();
            SendMessageW(frame,WM_LBUTTONDOWN,MK_LBUTTON,point);expect(GetFocus()==app.edit_,"pressing the return button keeps native text focus until release");
            SendMessageW(frame,WM_LBUTTONUP,0,point);expect(!app.edit_&&!app.edit_frame_&&app.document_.marks.back().text==L"水电费是的啊"&&app.document_.marks.back().color==state.color,"return button commits text and preserves its exact selected color");
        }
        app.BeginText(v,{750,450});RECT edge{};GetWindowRect(app.edit_frame_->Window(),&edge);
        expect(edge.left>=bounds.left&&edge.top>=bounds.top&&edge.right<=bounds.right&&edge.bottom<=bounds.bottom,"frame, return button and hints stay visible near a monitor edge");
        SetWindowTextW(app.edit_,L"边缘");Key(app.edit_,VK_RETURN);
        expect(app.document_.marks.back().a==Point{750,450}&&app.document_.marks.back().b.x<850,"repositioning the editor at a monitor edge preserves the requested annotation anchor and fits its content");
        state.dark=false;state.toolbar.scale=1.5f;app.document_.Reset();app.BeginText(v,{150,150});SetWindowTextW(app.edit_,L"地方没错");Key(app.edit_,VK_RETURN);
        auto short_text=app.document_.marks.back();const float short_width=short_text.b.x-short_text.a.x;
        expect(short_width>100&&short_width<160&&short_text.b.y-short_text.a.y<60,"four Chinese characters at 150% use a tight content box instead of the 450px input field");
        state.tool=Tool::Select;app.document_.selected=0;
        const auto handle=EditHandles(short_text,EditPart::Whole,state.toolbar.scale)[3].point;
        app.PointerDown(v,handle);app.PointerMove(v,{handle.x+short_width*.5f,handle.y},MK_LBUTTON);app.PointerUp(v,{handle.x+short_width*.5f,handle.y});
        expect(std::abs(app.document_.marks[0].font_size-short_text.font_size*1.5f)<.01f,"actual pointer dragging a text side handle enlarges its font without Shift");
        app.document_.Undo();expect(app.document_.marks[0]==short_text,"undo restores text dimensions and font size together");app.document_.Redo();expect(app.document_.marks[0].font_size>short_text.font_size,"redo restores enlarged glyphs");app.document_.Undo();state.tool=Tool::Text;
        {
            Mark sample=short_text;Translate(sample,{55-sample.a.x,65-sample.a.y});Document document;document.marks.push_back(sample);
            Renderer renderer;auto image=renderer.Flatten(MakeFrame({0,0,400,170},0xffffffff),document,{0,0,400,170});DibSurface bitmap(image.Width(),image.Height());std::copy(image.pixels.begin(),image.pixels.end(),bitmap.Pixels());
            ComPtr<ID2D1Factory> factory;D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf());ComPtr<ID2D1DCRenderTarget> target;
            const auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
            CheckWin32(SUCCEEDED(factory->CreateDCRenderTarget(&properties,&target)),"Bounds preview target");RECT rect{0,0,image.Width(),image.Height()};target->BindDC(bitmap.Dc(),&rect);target->BeginDraw();DrawSelectionEditor(target.Get(),sample,EditPart::Whole,1);CheckWin32(SUCCEEDED(target->EndDraw()),"Bounds preview");
            std::copy_n(bitmap.Pixels(),image.pixels.size(),image.pixels.begin());SavePng(image,L"docs/text-bounds-preview.png");
            Document sizes;for(float factor:{.65f,1.f,1.6f}){auto scaled=EditMark(sample,EditPart::Whole,3,{sample.b.x,(sample.a.y+sample.b.y)/2},{sample.a.x+(sample.b.x-sample.a.x)*factor,(sample.a.y+sample.b.y)/2},false,false);const float x=sizes.marks.empty()?30.f:sizes.marks.size()==1?170.f:370.f;Translate(scaled,{x-scaled.a.x,70-scaled.a.y});sizes.marks.push_back(scaled);}
            SavePng(renderer.Flatten(MakeFrame({0,0,640,180},0xffffffff),sizes,{0,0,640,180}),L"docs/text-scale-preview.png");
        }
        app.BeginText(v,short_text.a,0);SetWindowTextW(app.edit_,L"地方没错，文字变长");Key(app.edit_,VK_RETURN);expect(app.document_.marks[0].b.x-app.document_.marks[0].a.x>short_width,"automatic bounds grow when the text is edited to a longer phrase");
        app.BeginText(v,short_text.a,0);SetWindowTextW(app.edit_,L"短字");Key(app.edit_,VK_RETURN);expect(app.document_.marks[0].b.x-app.document_.marks[0].a.x<short_width,"automatic bounds shrink again when characters are removed");
        auto fixed=app.document_.marks[0];fixed=EditMark(fixed,EditPart::Whole,3,fixed.b,{fixed.b.x+100,fixed.b.y},false,false);app.document_.marks[0]=fixed;
        Renderer refresh_renderer;const auto before_edit=refresh_renderer.Flatten(*app.frame_,app.document_,app.frame_->bounds);
        app.BeginText(v,fixed.a,0);SetWindowTextW(app.edit_,L"新");Key(app.edit_,VK_RETURN);
        expect(app.document_.marks[0].text_auto_size&&app.document_.marks[0].text==L"新"&&app.document_.marks[0].font_size==fixed.font_size&&app.document_.marks[0].b.x-app.document_.marks[0].a.x<fixed.b.x-fixed.a.x,"confirming edited scaled text replaces its content and refits bounds while retaining enlarged font size");
        const auto after_edit=refresh_renderer.Flatten(*app.frame_,app.document_,app.frame_->bounds);Renderer fresh_renderer;
        expect(before_edit.pixels!=after_edit.pixels&&after_edit.pixels==fresh_renderer.Flatten(*app.frame_,app.document_,app.frame_->bounds).pixels,"reused text renderer paints the new scaled text without stale cached content");
        app.document_.Undo();expect(app.document_.marks[0]==fixed,"undo restores the prior scaled text and bounds together");app.document_.Redo();
        const auto current=app.document_.marks[0];state.tool=Tool::Text;
        app.PointerDown(v,{current.a.x+4,current.a.y+8});app.PointerUp(v,{current.a.x+4,current.a.y+8});
        expect(!app.edit_&&state.tool==Tool::Select&&app.document_.selected==0,"single click on existing text activates selection rather than creating or reopening text");
        app.PointerDoubleClick(v,{current.a.x+4,current.a.y+8});expect(app.edit_&&app.edit_index_==0,"double click reopens the directly selected text object");
        if(app.edit_){SendMessageW(app.edit_,EM_SETSEL,0,-1);SendMessageW(app.edit_,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"再改"));Key(app.edit_,VK_RETURN);}
        expect(app.document_.marks.size()==1&&app.document_.marks[0].text==L"再改","repeated pointer editing replaces the same text object without leaving the old content underneath");
        if(app.document_.marks.size()==1){
            const auto committed=app.document_.marks[0];const auto refreshed=refresh_renderer.Flatten(*app.frame_,app.document_,app.frame_->bounds);
            expect(refreshed.pixels!=after_edit.pixels&&refreshed.pixels==fresh_renderer.Flatten(*app.frame_,app.document_,app.frame_->bounds).pixels,"pointer-confirmed content is present in newly rendered pixels");
            SavePng(Crop(refreshed,{100,60,520,290}),L"docs/scaled-text-edit-preview.png");
            app.PointerDoubleClick(v,{committed.a.x+4,committed.a.y+8});SetWindowTextW(app.edit_,L"不保存");Key(app.edit_,VK_ESCAPE);
            expect(app.document_.marks.size()==1&&app.document_.marks[0]==committed,"cancelling a repeated pointer edit preserves the original scaled object without duplicates");
        }
        if(app.document_.marks.size()>1)app.document_.marks.resize(1);
        fixed=app.document_.marks[0];fixed.text_auto_size=false;app.document_.marks[0]=fixed;app.document_.selected=0;
        state.toolbar=PlaceToolbar(state.selection,bounds,1,1,Tool::Text);
        OpenToolbarDropdown(state,80,bounds);app.ApplyToolbarDropdown(static_cast<int>(TextFonts().size())-1);
        expect(app.document_.marks[0].font_family==TextFonts().back().family&&app.document_.marks[0].b==fixed.b,"font dropdown changes the active text while respecting a manually sized box");
        app.BeginText(v,fixed.a,0);LOGFONTW native{};GetObjectW(app.edit_font_,sizeof(native),&native);
        expect(std::wstring(native.lfFaceName)==TextFonts().back().family,"native text editing uses the selected family");app.CommitText(true);
        app.document_.Reset();state.font_family=TextFonts().back().family;app.BeginText(v,{150,150});SetWindowTextW(app.edit_,L"iiiiWWWW");Key(app.edit_,VK_RETURN);
        expect(app.document_.marks[0].font_family==state.font_family&&app.document_.marks[0].text_auto_size,"new text retains its chosen font with automatic sizing");
        Renderer font_renderer;const auto selected_font=font_renderer.Flatten(*app.frame_,app.document_,app.frame_->bounds);
        if(TextFonts().size()>1){app.document_.marks[0].font_family=TextFonts().front().family;FitTextBounds(app.document_.marks[0]);const auto different_font=font_renderer.Flatten(*app.frame_,app.document_,app.frame_->bounds);expect(selected_font.pixels!=different_font.pixels,"exported pixels and font cache follow font family changes");}
        Renderer toolbar_renderer;const auto toolbar_preview=toolbar_renderer.Demo(true,false,Tool::Text,false,-1,80);SavePng(Crop(toolbar_preview,{240,360,1125,740}),L"docs/text-font-picker-preview.png");
        TagEditorCases(app,v,expect);
        return failures?1:0;
    }
};
}
int main(int argc,char** argv){SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);int result=lumashot::TextEditTest::Run(argc==2&&std::string_view(argv[1])=="--number-label");CoUninitialize();return result;}
