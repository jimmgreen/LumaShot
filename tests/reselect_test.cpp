#include "app/application.h"
#include <iostream>
namespace lumashot {
struct ReselectTest {
    static int Run(bool session_tool_only=false) {
        std::cout<<std::unitbuf;
        int failures{};const auto expect=[&](bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<'\n';if(!ok)++failures;};
        {
            Application app;app.active_=true;
            static_cast<ToolProperties&>(app.state_)=app.preferences_.tools;
            for(int command:{0,1,2,3,4,5,6,14}){
                app.Command(command);
                expect(app.state_.tool==(command==14?Tool::Number:static_cast<Tool>(command)),"explicit toolbar command still selects its tool in the current session");
                expect(!app.properties_dirty_,"changing tools alone does not queue a preference save");
                app.state_={};app.ResetSessionTool();
                expect(app.state_.tool==Tool::Select&&app.toolbar_transition_.Value(GetTickCount64())==0&&!app.state_.selected&&!app.state_.picker.open&&!app.state_.dropdown.Open(),"each new session starts in Select with compact properties");
                static_cast<ToolProperties&>(app.state_)=app.preferences_.tools;
            }
            const auto path=std::filesystem::temp_directory_path()/(L"LumaShot-session-tool-test-"+std::to_wstring(GetCurrentProcessId())+L".ini");
            struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code error;std::filesystem::remove(path,error);}}cleanup{path};
            app.diagnostic_session_=true; // The isolated fixture never flushes personal settings.
            for(const auto legacy:{L"pen",L"text",L"number",L"arrow",L"invalid"}){
                WritePrivateProfileStringW(L"General",L"LastTool",legacy,path.c_str());
                Application restarted;restarted.preferences_=Preferences::LoadFrom(path);restarted.state_.tool=Tool::Pen;restarted.ResetSessionTool();
                expect(restarted.state_.tool==Tool::Select&&restarted.toolbar_transition_.Value(GetTickCount64())==0,"restart ignores legacy remembered tool values");
            }
            app.preferences_.tools.color=0xff123456;app.preferences_.SaveTo(path);
            wchar_t legacy[40]{};GetPrivateProfileStringW(L"General",L"LastTool",L"",legacy,40,path.c_str());
            expect(!legacy[0]&&Preferences::LoadFrom(path).tools.color==0xff123456,"settings save removes obsolete tool key but preserves style settings");
            app.diagnostic_session_=false;app.state_.tool=Tool::Text;
            app.Command(51);
            expect(app.preferences_.tools.text_bold&&app.properties_dirty_,"toolbar change updates persistent defaults");
            app.state_.picker.open=true;app.state_.picker.original=app.state_.color;
            app.state_.color=0xff112233;app.RememberProperties();
            expect(app.preferences_.tools.color!=0xff112233,"uncommitted picker preview is not persisted");
            app.ClosePicker(true);
            expect(app.preferences_.tools.color==app.state_.color,"picker cancellation retains committed color");
            app.state_.tool=Tool::Arrow;app.state_.dropdown.property=49;app.state_.dropdown.items={{L"Hand",3}};
            app.ApplyToolbarDropdown(0);
            expect(app.preferences_.tools.arrow_type==ArrowType::HandDrawn,"dropdown selection updates persistent defaults");
            app.diagnostic_session_=true;app.state_.arrow_size=61;app.RememberProperties();
            expect(app.preferences_.tools.arrow_size!=61,"diagnostic sessions never update personal defaults");
            // Isolated fixture never writes to the actual user's configuration.
            app.properties_dirty_=false;
        }
        if(session_tool_only)return failures?1:0;
        for(float offset:{0.f,-1280.f}) {
            Application app;const RECT screen{LONG(offset),0,LONG(offset)+1280,800};
            app.frame_=std::make_shared<Frame>(MakeFrame(screen,0xffedf2f7));app.monitors_={screen};
            auto view=std::make_unique<Application::View>();view->app=&app;view->bounds=screen;
            view->window=CreateWindowExW(0,L"STATIC",L"Synthetic reselection",WS_POPUP,LONG(offset),0,1280,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            if(!view->window)return 2;
            auto& v=*view;app.views_.push_back(std::move(view));
            auto& state=app.state_;state.selected=true;state.selection={offset+100,100,offset+400,350};state.tool=Tool::Rectangle;
            app.toolbar_transition_.Reset(1);app.UpdateToolbar({LONG(offset)+100,100});
            state.tool=Tool::Pen;app.Command(59);state.highlighter_color=0xffabc123;
            const float highlighter_width=24*state.toolbar.scale;
            app.PointerDown(v,{offset+160,180});app.PointerMove(v,{offset+260,180},MK_LBUTTON);app.PointerUp(v,{offset+260,180});
            expect(app.document_.marks.size()==1&&app.document_.marks[0].pen_mode==PenMode::Highlighter&&app.document_.marks[0].width==highlighter_width&&app.document_.marks[0].color==0xffabc123&&app.document_.marks[0].pen_opacity==.4f,"pointer input commits highlighter settings at monitor DPI");
            app.Command(7);expect(app.document_.marks.empty(),"highlighter undo removes the stroke");app.Command(8);
            expect(app.document_.marks.size()==1&&app.document_.marks[0].pen_mode==PenMode::Highlighter,"highlighter redo restores mode");
            app.Command(58);expect(state.pen_mode==PenMode::Normal&&state.ActiveWidth()==3,"normal mode restores pencil width");
            app.document_.Reset();
            state.tool=Tool::Text;app.Command(51);app.Command(54);app.Command(52);ChooseToolbarDropdown(state,3);
            app.BeginText(v,{offset+150,150});
            expect(app.edit_&&(GetWindowLongPtrW(app.edit_,GWL_STYLE)&ES_CENTER)!=0,"editor adopts centered text style");
            SetWindowTextW(app.edit_,L"Synthetic text");app.CommitText();
            expect(app.document_.marks.size()==1&&app.document_.marks[0].text_bold&&app.document_.marks[0].text_align==TextAlign::Center&&app.document_.marks[0].text_background==TextBackground::TagDark,"text commit preserves toolbar properties");
            state.text_bold=false;state.text_align=TextAlign::Left;state.text_background=TextBackground::None;
            app.document_.marks[0].rotation=31;app.document_.marks[0].text_auto_size=false;const auto text_geometry=app.document_.marks[0];
            app.PointerDoubleClick(v,{(text_geometry.a.x+text_geometry.b.x)/2,(text_geometry.a.y+text_geometry.b.y)/2});
            expect(app.edit_&&!app.pending_,"double click existing text edits instead of confirming capture");
            SetWindowTextW(app.edit_,L"Edited rotated text");app.CommitText();
            expect(app.document_.marks[0].rotation==31&&app.document_.marks[0].a==text_geometry.a&&app.document_.marks[0].b==text_geometry.b&&app.document_.marks[0].text==L"Edited rotated text","reediting rotated text preserves resize geometry and rotation");
            expect(app.document_.marks[0].text_bold&&app.document_.marks[0].text_align==TextAlign::Center&&app.document_.marks[0].text_background==TextBackground::TagDark,"reediting preserves existing text attributes");
            app.document_.Reset();state.tool=Tool::Arrow;state.arrow_type=ArrowType::HandDrawn;
            const Point loop_start{offset+180,180};app.PointerDown(v,loop_start);
            for(const Point p:{Point{offset+260,180},Point{offset+260,260},Point{offset+180,260},loop_start})app.PointerMove(v,p,MK_LBUTTON);
            app.PointerUp(v,loop_start);
            expect(app.document_.marks.size()==1&&app.document_.marks[0].a==app.document_.marks[0].b&&app.document_.marks[0].points.size()>=5,"hand drawn loop ending at its start is committed instead of discarded");
            app.Command(7);expect(app.document_.marks.empty(),"hand drawn loop undo");app.Command(8);expect(app.document_.marks.size()==1,"hand drawn loop redo");
            state.arrow_type=ArrowType::Standard;
            app.document_.Reset();state.tool=Tool::Rectangle;
            app.Command(14);state.number_combo=NumberCombo::Text;app.UpdateToolbar({LONG(offset)+100,100});
            const auto badge_color=state.color;app.Command(75);
            expect(state.number_text_preset==-1&&state.color==badge_color,"note preset changes only annotation text");
            app.Command(70);expect(state.picker.open&&state.picker_text_color,"note custom wheel opens the shared picker");
            state.PickerColor()=0xff123456;app.ClosePicker(true);
            expect(state.number_text_preset==-1&&state.color==badge_color&&state.number_text_color==*ToolbarColor(75),"cancel restores note preset without changing badge");
            state.number_combo=NumberCombo::Plain;
            state.tool=Tool::Number;app.PointerDown(v,{offset+200,180});app.PointerUp(v,{offset+200,180});app.PointerDown(v,{offset+250,180});app.PointerUp(v,{offset+250,180});
            expect(app.document_.marks.size()==2&&app.document_.marks[0].number==1&&app.document_.marks[1].number==2,"number tool places sequential badges on clicks");
            app.document_.Undo();app.PointerDown(v,{offset+280,180});app.PointerUp(v,{offset+280,180});
            expect(app.document_.marks.size()==2&&app.document_.marks.back().number==2,"undo permits the next number to be reused");
            app.document_.Reset();state.tool=Tool::Rectangle;
            app.Command(48);
            expect(state.dropdown.Open()&&state.dropdown.selected==0,"line style opens the themed popup with current selection");
            app.Key(v,VK_DOWN);app.Key(v,VK_RETURN);
            expect(!state.dropdown.Open()&&state.line_style==LineStyle::Dash,"keyboard selects and commits a popup item");
            app.Command(48);app.Key(v,VK_DOWN);app.Key(v,VK_ESCAPE);
            expect(!state.dropdown.Open()&&state.line_style==LineStyle::Dash&&state.selected,"Escape dismisses only the popup without changing the value");
            app.Command(48);const auto row=state.dropdown.Row(2);app.PointerDown(v,{(row.left+row.right)/2,(row.top+row.bottom)/2});
            expect(!state.dropdown.Open()&&state.line_style==LineStyle::Dot,"popup rows apply values by their painted hit area");
            app.Command(48);app.PointerDown(v,{offset+1100,20});
            expect(!state.dropdown.Open()&&state.selected&&!state.dragging,"outside click dismisses without starting a new capture selection");
            state.tool=Tool::Select;Mark editable;editable.a={offset+150,150};editable.b={offset+280,240};editable.corner_radius=18;editable.fill_color=0xffaaddff;app.document_.Add(editable);
            app.PointerDown(v,{offset+200,180});app.PointerUp(v,{offset+200,180});expect(app.document_.selected==0,"selection tool selects existing rectangle");
            auto controls=EditHandles(app.document_.marks[0],EditPart::Whole,state.toolbar.scale);auto grip=controls[4].point;app.PointerDown(v,grip);app.PointerMove(v,{grip.x+20,grip.y+15},MK_LBUTTON);app.PointerUp(v,{grip.x+20,grip.y+15});
            expect(app.document_.marks[0].b==Point{offset+300,255}&&state.selection.left==offset+100,"pointer resize edits rectangle instead of capture range");
            app.document_.Undo();expect(app.document_.marks[0].b==editable.b,"pointer edit undo restores prior rectangle");app.document_.Reset();state.tool=Tool::Rectangle;
            Mark mark;mark.a={offset+150,150};mark.b={offset+220,220};app.document_.Add(mark);
            mark.tool=Tool::Mosaic;app.document_.Add(mark);app.document_.Undo();
            app.draft_=mark;
            const auto* frozen=app.frame_.get();const auto pixels=app.frame_->pixels;
            const auto panel=state.toolbar.bounds;
            app.PointerDown(v,{panel.left+1,panel.top+1});
            expect(state.selected&&app.document_.marks.size()==1,"toolbar background does not restart selection");
            std::cout << "Panel " << panel.left << "," << panel.top << " - " << panel.right << "," << panel.bottom << '\n';
            app.PointerDown(v,{offset+1100,20});
            expect(state.dragging&&!state.selected&&state.tool==Tool::Select&&!app.draft_,"outside drag immediately starts a clean selection");
            expect(app.document_.marks.empty()&&!app.document_.CanUndo()&&!app.document_.CanRedo(),"old annotations and both history stacks are discarded");
            expect(GetCapture()==v.window,"new selection retains mouse capture");
            app.PointerMove(v,{offset+120,550},MK_LBUTTON);app.PointerUp(v,{offset+120,550});
            expect(state.selected&&!state.dragging&&state.selection.left==offset+120&&state.selection.right==offset+1100&&state.selection.top==20&&state.selection.bottom==550,"reverse drag establishes the new physical range");
            expect(!app.toolbar_transition_.Active(GetTickCount64())&&app.toolbar_transition_.Value(GetTickCount64())==0,"replacement range returns to compact selection toolbar");
            app.document_.Undo();app.document_.Redo();
            Renderer renderer;const auto output=renderer.Flatten(*app.frame_,app.document_,PixelRect(state.selection));
            expect(output.pixels==Crop(*app.frame_,PixelRect(state.selection)).pixels,"export of overlapping replacement range contains no previous annotation");
            expect(app.frame_.get()==frozen&&app.frame_->pixels==pixels,"reselection preserves the frozen desktop image");
            app.document_.Add(mark);
            app.PointerDown(v,{offset+120,20});app.PointerMove(v,{offset+110,10},MK_LBUTTON);app.PointerUp(v,{offset+110,10});
            expect(state.selection.left==offset+110&&app.document_.marks.size()==1,"selection handle resize preserves annotations");
            app.PointerDown(v,{offset+500,300});app.PointerMove(v,{offset+510,310},MK_LBUTTON);app.PointerUp(v,{offset+510,310});
            expect(state.selection.left==offset+120&&app.document_.marks.size()==1,"moving selection preserves annotations");
            const Point outside{float(screen.left)+20,float(screen.top)+20};
            expect(!Contains(state.toolbar.bounds,outside)&&!Contains(state.selection,outside),"reselection fixture is outside the current toolbar and capture range");
            state.busy=true;app.PointerDown(v,outside);
            expect(state.selected&&app.document_.marks.size()==1,"export busy state cannot be replaced");state.busy=false;
            state.tool=Tool::Text;app.PointerDown(v,outside);app.PointerUp(v,{outside.x+100,outside.y+60});
            expect(state.selected&&state.tool==Tool::Select&&app.document_.marks.empty(),"reselection also exits the previous annotation tool");
        }
        for(float offset:{0.f,-1280.f}){
            Application app;app.diagnostic_session_=true;app.active_=true;
            app.frame_=std::make_shared<Frame>(MakeFrame({LONG(offset),0,LONG(offset)+800,600},0xffeef3f8));
            Application::View view;view.app=&app;view.bounds=app.frame_->bounds;
            app.state_.selected=true;app.state_.selection={offset+100,100,offset+500,400};
            app.state_.toolbar.bounds={offset+120,350,offset+450,390};
            app.PointerDoubleClick(view,{offset+50,150});expect(!app.pending_,"double click outside selection does not confirm");
            app.PointerDoubleClick(view,{offset+200,370});expect(!app.pending_,"double click toolbar does not confirm selection");
            app.state_.selected=false;app.PointerDoubleClick(view,{offset+200,200});expect(!app.pending_,"hover-only region does not confirm");app.state_.selected=true;
            app.state_.busy=true;app.PointerDoubleClick(view,{offset+200,200});expect(!app.pending_,"double click while busy ignored");app.state_.busy=false;
            app.PointerDoubleClick(view,{offset+200,200});expect(app.pending_&&app.state_.busy,"double click selected area starts the Enter completion path");
            app.worker_.join();expect(app.result_&&app.result_->frame.Width()==400&&app.result_->frame.Height()==300,"double click exports selected physical-pixel dimensions");
            // Do not deliver the export result: fixtures never change the clipboard.
        }
        return failures?1:0;
    }
};
}
int main(int argc,char** argv){SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);const int result=lumashot::ReselectTest::Run(argc==2&&std::string_view(argv[1])=="--session-tool");CoUninitialize();return result;}


