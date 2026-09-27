#include "app/application.h"
#include "export/png.h"
#include "ui/selection_cursor.h"
#include <wincodec.h>
#include <iostream>
#include "number_label_cases.h"
#include "annotation_palette_cases.h"
namespace lumashot {
struct SelectedPropertiesTest {
#include "number_label_pointer_cases.h"
#include "instant_property_cases.h"
#include "pen_line_cases.h"
#include "polyline_cases.h"
#include "rotation_cursor_cases.h"
#include "dashed_box_cases.h"
static int Run(bool numberReconnectOnly=false,bool numberLabelOnly=false,bool paletteOnly=false,bool penLineOnly=false,bool polylineOnly=false,bool rotationCursorOnly=false){
 int failures=0;auto expect=[&](bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<std::endl;failures+=!ok;};
 Application app;app.diagnostic_session_=true;app.active_=true;app.frame_=std::make_shared<Frame>(MakeFrame({0,0,1280,900},0xffeef2f6));app.monitors_={{0,0,1280,900}};app.toolbar_monitor_=app.monitors_[0];
 auto owned=std::make_unique<Application::View>();owned->app=&app;owned->bounds=app.monitors_[0];owned->window=CreateWindowExW(0,L"STATIC",L"Synthetic property editor",WS_POPUP,0,0,1280,900,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);auto& view=*owned;app.views_.push_back(std::move(owned));
 auto& state=app.state_;state.selected=true;state.selection={30,30,900,700};state.tool=Tool::Select;app.UpdateToolbar({150,150});
 if(rotationCursorOnly){RotationCursorCases(app,view,expect);return failures?1:0;}
 if(polylineOnly){PolylineCases(app,view,expect);return failures?1:0;}
 if(penLineOnly){PenLineCases(app,view,expect);return failures?1:0;}
 if(paletteOnly){AnnotationPaletteCases(expect);return failures?1:0;}
 if(numberLabelOnly){NumberLabelCases(expect);LabelPointerCases(app,view,expect);return failures?1:0;}
 if(numberReconnectOnly){
     DashedBoxCases(app,view,expect);
     NumberReconnectModelCases(expect);
      for(int corner=0;corner<4;++corner)for(float rotation:{0.f,30.f})for(float scale:{1.f,1.5f,2.f})for(int handle=0;handle<8;++handle){
          auto mark=NoteResizeFixture(corner);mark.rotation=rotation;app.document_.Reset();app.document_.marks={mark};app.document_.selected=0;app.document_.selected_part=EditPart::Detail;state.tool=Tool::Select;state.toolbar.scale=scale;
          const auto grip=EditHandles(mark,EditPart::Detail,scale)[handle].point;
          const Point destination{grip.x+24,grip.y+18};
          app.PointerDown(view,grip);expect(app.mark_handle_==handle,"pointer selects the intended note resize handle at each DPI");
          app.PointerMove(view,{grip.x+8,grip.y+6},MK_LBUTTON);app.PointerMove(view,destination,MK_LBUTTON);app.PointerUp(view,destination);
          const auto changed=app.document_.marks[0];expect(Near(NoteBadgeOffset(changed,corner%2!=0,corner>=2),NoteBadgeOffset(mark,corner%2!=0,corner>=2)),"real multi-event note resize retains badge relative position");
          expect(changed.number_size==mark.number_size,"real note resize does not scale the badge");
          app.Command(7);expect(app.document_.marks[0]==mark&&!app.document_.CanUndo(),"real linked resize is one undo checkpoint");app.Command(8);expect(app.document_.marks[0]==changed,"real linked resize redo includes badge position");
      }

      for(bool custom:{false,true})for(float rotation:{0.f,30.f,-45.f})for(float scale:{1.f,1.5f,2.f}){
          auto mark=NumberFixture(NumberCombo::Text,custom);mark.number_label=L"看这里";mark.text.clear();mark.rotation=rotation;
          app.document_.Reset();app.document_.marks={mark};state.tool=Tool::Select;state.toolbar.scale=scale;
          const auto detail=NumberDetailBounds(mark);const auto start=WorldPoint(mark,{(detail.left+detail.right)/2,(detail.top+detail.bottom)/2});
          app.PointerDown(view,start);app.PointerUp(view,start);
          expect(app.document_.selected_part==EditPart::Detail&&app.document_.marks[0]==mark&&!app.document_.CanUndo(),"real note body selection preserves mark and separate edit target");
          const Point destination{start.x+60,start.y+35};
          app.PointerDown(view,start);app.PointerMove(view,{start.x+20,start.y+10},MK_LBUTTON);app.PointerMove(view,destination,MK_LBUTTON);app.PointerUp(view,destination);
          auto expected=mark;Translate(expected,{destination.x-start.x,destination.y-start.y});
          expect(app.document_.marks[0]==expected,"real pointer note drag moves caption badge and placeholder together at every DPI");
          app.Command(7);expect(app.document_.marks[0]==mark&&!app.document_.CanUndo(),"multi-event note drag creates exactly one undo checkpoint");
          app.Command(8);expect(app.document_.marks[0]==expected,"pointer note group redo restores complete combination");
          const Point again{destination.x-25,destination.y+15};app.PointerDown(view,destination);app.PointerMove(view,again,MK_LBUTTON);app.PointerUp(view,again);
          Translate(expected,{again.x-destination.x,again.y-destination.y});expect(app.document_.marks[0]==expected,"second note drag keeps existing badge-detail offset without cumulative separation");
      }

     for(auto combo:{NumberCombo::Leader,NumberCombo::DashedBox,NumberCombo::Highlight,NumberCombo::Text})for(bool custom:{false,true}){
         auto mark=NumberFixture(combo,custom);app.document_.Reset();app.document_.marks={mark};state.tool=Tool::Select;
         const auto start=BadgeCenter(mark);app.PointerDown(view,start);app.PointerUp(view,start);
         expect(app.document_.selected_part==EditPart::Badge&&app.document_.marks[0]==mark&&!app.document_.CanUndo(),"selecting an existing number preserves its combination without history");
         app.PointerDown(view,start);app.PointerMove(view,{start.x+40,start.y+20},MK_LBUTTON);app.PointerMove(view,{start.x+120,start.y+50},MK_LBUTTON);app.PointerUp(view,{start.x+120,start.y+50});
         const auto changed=app.document_.marks[0];expect(NumberAttached(changed)&&Near(BadgeCenter(changed),{start.x+120,start.y+50}),"real repeated-edit drag reconnects badge and component");
         app.Command(7);expect(app.document_.marks[0]==mark&&!app.document_.CanUndo(),"multi-move pointer gesture is one undo step");app.Command(8);expect(app.document_.marks[0]==changed,"UI redo restores connected geometry");
         const auto again=BadgeCenter(changed);app.PointerDown(view,again);app.PointerMove(view,{again.x-55,again.y+40},MK_LBUTTON);app.PointerUp(view,{again.x-55,again.y+40});expect(NumberAttached(app.document_.marks[0]),"a second edit stays connected instead of freezing old geometry");
     }
      for(bool custom:{false,true})for(int handle=20;handle<=23;++handle){
          auto mark=NumberFixture(NumberCombo::Leader,custom);app.document_.Reset();app.document_.marks={mark};
          app.document_.selected=0;app.document_.selected_part=EditPart::Leader;state.tool=Tool::Select;
          const auto start=EditHandles(mark,EditPart::Leader,1)[handle-20].point;
          const Point end{start.x+70,start.y+85};
          app.PointerDown(view,start);expect(app.mark_handle_==handle,"real pointer selects the intended leader handle");
          app.PointerMove(view,{start.x+30,start.y+25},MK_LBUTTON);app.PointerMove(view,end,MK_LBUTTON);app.PointerUp(view,end);
          const auto changed=app.document_.marks[0];expect(NumberAttached(changed),"real leader-handle gesture stays attached");
          if(handle==20)expect(Near(BadgeCenter(changed),{BadgeCenter(mark).x+70,BadgeCenter(mark).y+85}),"real attachment drag moves badge");
          app.Command(7);expect(app.document_.marks[0]==mark&&!app.document_.CanUndo(),"leader-handle gesture undoes in one step");
          app.Command(8);expect(app.document_.marks[0]==changed,"leader-handle redo restores attachment");
      }
     return failures?1:0;
 }
 InstantPropertyCases(app,view,expect);app.document_.Reset();app.Command(0);
 Mark rectangle;rectangle.tool=Tool::Rectangle;rectangle.a={100,100};rectangle.b={240,230};rectangle.width=4;rectangle.color=0xff667788;rectangle.fill_color=0xffaabbcc;rectangle.fill_opacity=.45f;
 Mark arrow;arrow.tool=Tool::Arrow;arrow.a={350,100};arrow.b={450,230};arrow.arrow_size=23;arrow.width=6;arrow.color=0xff112233;
 app.document_.marks={rectangle,arrow};app.PointerDown(view,{100,160});app.PointerUp(view,{100,160});app.toolbar_transition_.Reset(1);app.AnimateToolbar();
 expect(state.tool==Tool::Select&&state.PropertyTool()==Tool::Rectangle&&app.document_.selected==0,"selecting rectangle keeps selection tool and opens rectangle properties");expect(state.width==4/app.property_scale_&&state.color==rectangle.color&&state.fill_opacity==rectangle.fill_opacity,"panel reads object values instead of drawing defaults");
 const auto controls=ToolbarControls(state,app.document_);bool field=false;for(const auto& c:controls)if(c.id==47)field=c.enabled;expect(field,"selected object property control enabled");
 app.Command(21);expect(app.document_.marks[0].color==*ToolbarColor(21)&&app.document_.marks[1]==arrow,"color button edits only selected mark");
 app.Command(7);expect(app.document_.marks[0]==rectangle,"property button edit can be undone");app.Command(8);expect(app.document_.marks[0].color==*ToolbarColor(21),"property edit can be redone");
 app.PointerDown(view,{350,100});app.PointerUp(view,{350,100});app.toolbar_transition_.Reset(1);app.AnimateToolbar();expect(state.tool==Tool::Select&&state.PropertyTool()==Tool::Arrow&&state.arrow_size==23/app.property_scale_,"selecting another object replaces property panel and values");
 app.Command(50);int item=-1;for(int i=0;i<static_cast<int>(state.dropdown.items.size());++i)if(state.dropdown.items[i].second==32)item=i;app.ApplyToolbarDropdown(item);expect(app.document_.marks[1].arrow_size==32*app.property_scale_&&app.document_.marks[1].a==arrow.a&&app.document_.marks[1].b==arrow.b,"dropdown edits arrow head size without changing endpoints");
 // Start with no undo history to verify preview cancellation creates none.
 app.document_.Reset();app.document_.marks={rectangle};app.document_.selected=0;app.SyncSelectedProperties();app.toolbar_transition_.Reset(1);app.AnimateToolbar();
 app.Command(28);state.PickerColor()=0xff123456;app.ApplySelectedProperties(true);expect(app.document_.marks[0].color==0xff123456,"picker previews object color");app.ClosePicker(true);expect(app.document_.marks[0]==rectangle&&!app.document_.CanUndo(),"cancel picker restores mark without an undo entry");
 state.fill_opacity=.2f;app.ApplySelectedProperties(true);state.fill_opacity=.1f;app.ApplySelectedProperties(true);app.ApplySelectedProperties();expect(app.document_.marks[0].fill_opacity==.1f,"slider preview commits final value");app.Command(7);expect(app.document_.marks[0]==rectangle&&!app.document_.CanUndo(),"slider gesture forms one undo step");
 app.PointerDown(view,{650,400});app.PointerUp(view,{650,400});expect(state.PropertyTool()==Tool::Select&&app.document_.selected==-1,"blank click clears object property context");
 app.ocr_available_=false;app.UpdateToolbar({150,150});app.Command(15);expect(!app.pending_&&!state.toolbar.ocr_available,"missing OCR capability hides action and rejects command");
 // All drawing tools can directly select each existing annotation kind.
 const auto choose=[&](Tool tool){app.Command(tool==Tool::Number?14:static_cast<int>(tool));};
 const auto reset=[&](const Mark& mark,Tool tool){
     app.document_.Reset();app.document_.marks={mark};app.pin_edit_id_=0;
     state.selected=true;state.selection={30,30,900,700};choose(tool);app.UpdateToolbar({150,150});
 };
 std::vector<std::pair<Mark,Point>> samples;
 samples.push_back({rectangle,{150,160}});
 auto outline=rectangle;outline.fill_color.reset();samples.push_back({outline,{100,160}});
 auto ellipse=outline;ellipse.tool=Tool::Ellipse;samples.push_back({ellipse,{240,165}});
 samples.push_back({arrow,{400,165}});
 Mark pen;pen.tool=Tool::Pen;pen.a={100,100};pen.b={240,230};pen.points={{100,100},{170,160},{240,230}};samples.push_back({pen,{170,160}});
 pen.pen_mode=PenMode::Highlighter;samples.push_back({pen,{170,160}});
 Mark text;text.tool=Tool::Text;text.a={100,100};text.b={240,150};text.text=L"Synthetic annotation";samples.push_back({text,{170,125}});
 Mark mosaic;mosaic.tool=Tool::Mosaic;mosaic.mosaic_method=MosaicMethod::Rectangle;mosaic.a={100,100};mosaic.b={240,230};samples.push_back({mosaic,{170,160}});
 Mark number;number.tool=Tool::Number;number.a={100,100};number.b={132,132};samples.push_back({number,{116,116}});
 number.number_combo=NumberCombo::Text;number.text=L"Synthetic note";number.number_target={350,180};number.number_detail=Box{200,100,380,160};samples.push_back({number,{280,130}});
 number.number_combo=NumberCombo::Leader;number.number_leader=std::array<Point,4>{Point{116,116},Point{180,116},Point{220,170},Point{300,170}};samples.push_back({number,{260,170}});
 for(Tool tool:{Tool::Select,Tool::Rectangle,Tool::Ellipse,Tool::Arrow,Tool::Pen,Tool::Text,Tool::Mosaic,Tool::Number}){
     for(const auto& [mark,point]:samples){
         reset(mark,tool);app.PointerDown(view,point);app.PointerUp(view,point);
         expect(state.tool==Tool::Select&&state.PropertyTool()==mark.tool&&app.document_.selected==0&&!app.draft_&&!app.edit_,"clicking an existing annotation from any tool selects it and exposes its properties");
         expect(app.document_.marks.size()==1&&app.document_.marks[0]==mark&&!app.document_.CanUndo(),"selection-only click neither alters an object nor adds an undo entry");
     }
 }
 reset(rectangle,Tool::Pen);const Point from{150,160},to{170,175};
 app.PointerDown(view,from);app.PointerMove(view,to,MK_LBUTTON);app.PointerUp(view,to);
 expect(app.document_.marks.size()==1&&app.document_.marks[0].a==Point{120,115},"direct selection immediately supports dragging without drawing a second mark");
 app.Command(7);expect(app.document_.marks[0]==rectangle&&!app.document_.CanUndo()&&app.document_.CanRedo(),"direct drag is one undoable action");
 choose(Tool::Ellipse);app.PointerDown(view,from);app.PointerUp(view,from);
 expect(app.document_.CanRedo(),"selection-only click preserves redo history");app.Command(8);
 expect(app.document_.marks[0].a==Point{120,115},"redo restores the direct drag after an intervening selection");
 reset(rectangle,Tool::Pen);app.PointerDown(view,from);app.PointerUp(view,from);app.Command(21);
 expect(app.document_.marks[0].color==*ToolbarColor(21),"directly selected object is editable using the existing color controls");
 reset(outline,Tool::Ellipse);app.PointerDown(view,{170,165});
 expect(state.tool==Tool::Ellipse&&app.draft_&&app.document_.selected==-1,"empty interior of an outline remains available for drawing");app.PointerUp(view,{190,185});
 expect(app.document_.marks.size()==2&&app.document_.marks[0]==outline,"drawing in empty space preserves the existing annotation");
 reset(rectangle,Tool::Mosaic);auto top=rectangle;top.color=0xff334455;app.document_.marks.push_back(top);
 app.PointerDown(view,from);app.PointerUp(view,from);expect(app.document_.selected==1,"overlapping annotations select the topmost hit");
 reset(rectangle,Tool::Arrow);app.pin_edit_id_=77;app.PointerDown(view,from);app.PointerUp(view,from);
 expect(state.tool==Tool::Select&&app.document_.selected==0,"pinned-image annotation sessions use the same direct selection path");app.pin_edit_id_=0;
 reset(rectangle,Tool::Pen);const Point outside{1100,20};
 expect(!Contains(state.toolbar.bounds,outside)&&!Contains(state.selection,outside),"outside-range fixture avoids toolbar and capture bounds");app.PointerDown(view,outside);
 expect(!state.selected&&state.dragging&&app.document_.marks.empty(),"outside-range clicks retain the fresh screenshot selection behavior");
 return failures?1:0;
}
};
}
int main(int argc,char** argv){SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);const int result=lumashot::SelectedPropertiesTest::Run(argc==2&&std::string_view(argv[1])=="--number-reconnect",argc==2&&std::string_view(argv[1])=="--number-label",argc==2&&std::string_view(argv[1])=="--palette",argc==2&&std::string_view(argv[1])=="--pen-line",argc==2&&std::string_view(argv[1])=="--polyline",argc==2&&std::string_view(argv[1])=="--rotation-cursor");CoUninitialize();return result;}

