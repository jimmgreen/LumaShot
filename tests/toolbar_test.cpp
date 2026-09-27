#include "ui/render.h"
#include "model/arrow.h"
#include "ui/fonts.h"
#include "export/png.h"
#include <iostream>

using namespace lumashot;
int main(int argc,char** argv) {
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 2;
    int failures=0;
    const auto expect=[&](bool ok,const char* name){std::cout<<(ok?"[PASS] ":"[FAIL] ")<<name<<'\n';if(!ok)++failures;};
    try {
        if(argc>=2&&std::string_view(argv[1])=="--record-icon"){
            for(float scale:{1.f,1.5f,2.f})for(int width:{480,800,1280})for(bool ocr:{false,true}){
                ViewState state;state.selected=true;state.selection={-400,100,-50,400};state.toolbar=PlaceToolbar(state.selection,{-width,0,0,800},scale);state.toolbar.ocr_available=ocr;
                const auto controls=ToolbarControls(state,Document{});const auto icon=std::find_if(controls.begin(),controls.end(),[](const auto& c){return c.id==16;});
                expect(icon!=controls.end()&&icon->kind==ui::Kind::Button&&icon->icon==16&&icon->text.empty(),"single recording icon, no label or dropdown arrow");
                const auto b=icon->bounds,cancel=state.toolbar.Button(11);expect(state.toolbar.Hit({(b.left+b.right)/2,(b.top+b.bottom)/2},Tool::Select)==16&&b.right<cancel.left&&std::abs((b.right-b.left)-(cancel.right-cancel.left))<.01f,"record icon uses one action slot and correct DPI/negative-origin hit target");
                expect(!OpenToolbarDropdown(state,16,{-width,0,0,800})&&!state.dropdown.Open(),"recording dropdown removed");
                state.busy=true;expect(ui::HitTest(ToolbarControls(state,Document{}),{(b.left+b.right)/2,(b.top+b.bottom)/2})!=16,"busy recording icon disabled");
                state.busy=false;state.selection.right=state.selection.left+7;expect(ui::HitTest(ToolbarControls(state,Document{}),{(b.left+b.right)/2,(b.top+b.bottom)/2})!=16,"tiny recording selection disabled");
                state.toolbar.recording_available=false;const auto pinned=state.toolbar.Controls(Tool::Select);expect(std::none_of(pinned.begin(),pinned.end(),[](const auto& c){return c.id==16;}),"pinned-image editor hides recording icon");
            }
            if(argc>=3){const std::filesystem::path folder=argv[2];std::filesystem::create_directories(folder);for(bool dark:{false,true})for(bool hover:{false,true}){Renderer renderer;auto frame=renderer.Demo(true,dark,Tool::Select,false,hover?16:-1);SavePng(frame,folder/(std::string(dark?"dark":"light")+(hover?"-hover.png":".png")));}}
            CoUninitialize();return failures?1:0;
        }
        for(float scale:{1.0f,1.5f,2.0f})for(int width:{480,800,1280})for(Tool tool:{Tool::Select,Tool::Rectangle,Tool::Ellipse,Tool::Arrow,Tool::Pen,Tool::Text,Tool::Mosaic,Tool::Number})for(auto combo:{NumberCombo::Plain,NumberCombo::Text}) {
            const auto t=PlaceToolbar({-300,200,-40,600},{-width,0,0,1080},scale,1,tool,combo);
            bool valid=t.bounds.top>=0&&t.bounds.bottom<=1080&&t.bounds.left>=-width&&t.bounds.right<=0;
            const auto controls=t.Controls(tool);
            const auto pin=t.Button(13),ocr=t.Button(15),record=t.Button(16),cancel=t.Button(11);
            valid=valid&&pin.right<ocr.left&&ocr.right<record.left&&record.right<cancel.left&&record.right-record.left>=18*t.scale&&std::abs((record.right-record.left)-(cancel.right-cancel.left))<.01f&&ocr.right-ocr.left>=38*t.scale;
            const auto entry=std::find_if(controls.begin(),controls.end(),[](const ui::Control& c){return c.id==15;});
            valid=valid&&entry!=controls.end()&&entry->text==L"OCR"&&entry->icon==-1;
            for(const auto& c:controls){const auto p=c.bounds;valid=valid&&Contains(t.bounds,{p.left,p.top})&&Contains(t.bounds,{p.right,p.bottom});if(c.Interactive())valid=valid&&t.Hit({(p.left+p.right)/2,(p.top+p.bottom)/2},tool)==c.id;}
            for(size_t a=0;a<controls.size();++a)for(size_t b=a+1;b<controls.size();++b)if(controls[a].Interactive()&&controls[b].Interactive()){
                const auto x=controls[a].bounds,y=controls[b].bounds;valid=valid&&!(std::min(x.right,y.right)>std::max(x.left,y.left)&&std::min(x.bottom,y.bottom)>std::max(x.top,y.top));
            }
            expect(valid,"property layout and hit regions fit narrow/negative-origin/DPI display");
        }
        for(auto tool:{Tool::Rectangle,Tool::Ellipse,Tool::Arrow,Tool::Pen,Tool::Text,Tool::Mosaic,Tool::Number})for(auto combo:{NumberCombo::Plain,NumberCombo::Text}){
            const auto layout=PlaceToolbar({100,100,1100,500},{0,0,1280,800},1,1,tool,combo);
            const bool note=tool==Tool::Number&&combo==NumberCombo::Text;const float last_row=note?62.f:0.f;
            const auto controls=layout.Controls(tool);float top=-1,max_offset=0;bool aligned=true;
            for(const auto& c:controls)if(c.Interactive()&&c.id>=20){if(top<0)top=c.bounds.top;const float offset=c.bounds.top-top;max_offset=std::max(max_offset,offset);aligned=aligned&&(std::abs(offset)<.01f||std::abs(offset-last_row)<.01f)&&std::abs(c.bounds.bottom-c.bounds.top-28)<.01f;}
            expect(aligned&&std::abs(max_offset-last_row)<.01f&&layout.bounds.bottom-layout.bounds.top<=122+last_row,"wide properties use one aligned row, or two for a number note with custom label");
        }
        {
            ViewState record;record.selected=true;record.selection={-400,200,-100,400};
            record.toolbar=PlaceToolbar(record.selection,{-800,0,0,600},1.5f);
            const auto controls=record.toolbar.Controls(Tool::Select);
            const auto icon=std::find_if(controls.begin(),controls.end(),[](const auto& c){return c.id==16;});
            expect(icon!=controls.end()&&icon->kind==ui::Kind::Button&&icon->icon==16&&icon->text.empty(),"record entry is one icon button without text or dropdown");
            expect(!OpenToolbarDropdown(record,16,{-800,0,0,600}),"record entry has no video/GIF dropdown");
            record.toolbar.recording_available=false;
            const auto pinned=record.toolbar.Controls(Tool::Select);expect(std::none_of(pinned.begin(),pinned.end(),[](const auto& c){return c.id==16;}),"pinned-image annotation omits recording entry");
        }
        ViewState state;state.selected=true;state.tool=Tool::Text;
        state.toolbar=PlaceToolbar({100,100,1100,500},{0,0,1280,800},1,1,Tool::Text);
        expect(OpenToolbarDropdown(state,80,{0,0,1280,800})&&state.dropdown.items.size()==TextFonts().size(),"font dropdown lists available common system fonts");
        ChooseToolbarDropdown(state,static_cast<int>(TextFonts().size())-1);expect(state.font_family==TextFonts().back().family,"font selection stores the actual font family");
        expect(AdjustPropertyValue(state,24,4)&&state.font_size==28&&state.width==3,"text adjustment changes font size only");
        AdjustPropertyValue(state,27,-1000);expect(state.font_size==8,"font lower bound");
        state.tool=Tool::Mosaic;AdjustPropertyValue(state,24,8);
        expect(state.mosaic_brush==40&&state.mosaic_cell==12,"brush adjustment preserves mosaic cell size");
        AdjustPropertyValue(state,27,4);expect(state.mosaic_cell==16&&state.mosaic_brush==40,"cell adjustment preserves brush size");
        state.tool=Tool::Pen;AdjustPropertyValue(state,27,1000);expect(state.width==16,"stroke width upper bound");
        state.pen_mode=PenMode::Highlighter;
        expect(state.ActiveWidth()==24&&state.ActiveColor()==0xffe8b339&&state.PenOpacity()==.4f,"highlighter defaults to a broad translucent gold stroke");
        AdjustPropertyValue(state,27,1000);expect(state.ActiveWidth()==64&&state.width==16,"highlighter width clamps independently");
        AdjustPropertyValue(state,56,20);expect(std::abs(state.PenOpacity()-.2f)<.001f&&state.pen_opacity==1,"highlighter opacity is independent");
        state.toolbar=PlaceToolbar({100,100,400,400},{0,0,1280,800},1,1,state.tool);
        expect(OpenToolbarDropdown(state,27,{0,0,1280,800})&&state.dropdown.items[3].second==24,"highlighter menu offers broad widths");ChooseToolbarDropdown(state,3);
        state.ActiveColor()=0xffff68c8;state.tool=Tool::Rectangle;
        expect(state.ActiveWidth()==16&&state.ActiveColor()==state.color,"other tools retain their original style");
        state.tool=Tool::Pen;expect(state.ActiveWidth()==24&&state.ActiveColor()==0xffff68c8,"returning to highlighter restores custom style");
        state.pen_mode=PenMode::Normal;
        state.tool=Tool::Select;expect(!AdjustPropertyValue(state,27,1),"selection tool has no numeric properties");
        state.tool=Tool::Text;state.busy=true;expect(!AdjustPropertyValue(state,27,1),"busy state ignores property edits");
        state.busy=false;state.tool=Tool::Arrow;state.arrow_size=12;state.width=3;
        AdjustPropertyValue(state,50,1000);expect(state.arrow_size==64&&state.width==3,"arrow size clamps independently of line width");
        AdjustPropertyValue(state,50,-1000);expect(state.arrow_size==4,"arrow size has a safe lower bound");
        for(auto tool:{Tool::Rectangle,Tool::Arrow,Tool::Text}){
            state.tool=tool;state.toolbar=PlaceToolbar({0,0,300,300},{0,0,1280,1080},1,1,state.tool);
            Document document;const auto controls=ToolbarControls(state,document);
            for(int id:(tool==Tool::Arrow?std::vector<int>{48,49,50}:(tool==Tool::Rectangle?std::vector<int>{48}:std::vector<int>{27,80}))){
                const auto b=state.toolbar.Property(id,tool);
                expect(ui::HitTest(controls,{(b.left+b.right)/2,(b.top+b.bottom)/2})==id,"new property uses the visible control hit area");
            }
        }
        if(argc==2) {
            state.tool=Tool::Arrow;state.toolbar=PlaceToolbar({100,100,400,400},{0,0,1280,800},1,1,state.tool);
            expect(OpenToolbarDropdown(state,49,{0,0,1280,800})&&state.dropdown.items.size()==6,"arrow type menu exposes six types");ChooseToolbarDropdown(state,3);
            expect(OpenToolbarDropdown(state,66,{0,0,1280,800})&&state.dropdown.items.size()==11,"arrow head menu exposes eleven heads");ChooseToolbarDropdown(state,9);
            expect(state.arrow_type==ArrowType::HandDrawn&&state.arrow_head==ArrowHead::Slanted,"arrow type and head can be combined independently");
            state.tool=Tool::Mosaic;state.mosaic_method=MosaicMethod::Brush;state.mosaic_strength=70;
            AdjustPropertyValue(state,65,1000);expect(state.mosaic_strength==100,"mosaic strength clamps at one hundred percent");
            AdjustPropertyValue(state,65,-1000);expect(state.mosaic_strength==1,"mosaic strength keeps a nonzero effect");
            state.toolbar=PlaceToolbar({100,100,400,400},{0,0,1280,800},1,1,state.tool);Document mosaic_document;
            for(int id:{60,61,62,63,64,65}){const auto b=state.toolbar.Property(id,Tool::Mosaic);expect(ui::HitTest(ToolbarControls(state,mosaic_document),{(b.left+b.right)/2,(b.top+b.bottom)/2})==id,"mosaic option hit area matches painting");}
            state.tool=Tool::Pen;state.pen_opacity=1;AdjustPropertyValue(state,56,80);expect(std::abs(state.pen_opacity-.2f)<.001f,"pen transparency maps to inverse stroke opacity");
            expect(OpenToolbarDropdown(state,57,{-1280,-200,0,700}),"pen smoothing opens themed dropdown");ChooseToolbarDropdown(state,2);expect(state.pen_smoothing==PenSmoothing::High,"pen smoothing dropdown applies high smoothing");
            for(float scale:{1.f,1.5f,2.f})for(int id:{24,27,48,49,50}){
                state.tool=id==24?Tool::Mosaic:(id==27?Tool::Text:Tool::Arrow);
                const RECT monitor{-1280,-200,0,700};state.toolbar=PlaceToolbar({-300,300,-10,680},monitor,scale,1,state.tool);
                expect(OpenToolbarDropdown(state,id,monitor),"every dropdown uses the shared popup");
                const auto bounds=state.dropdown.bounds;
                expect(bounds.left>=monitor.left&&bounds.right<=monitor.right&&bounds.top>=monitor.top&&bounds.bottom<=monitor.bottom,"popup stays within negative-origin screen at mixed DPI");
                for(int i=0;i<static_cast<int>(state.dropdown.items.size());++i){const auto row=state.dropdown.Row(i);expect(state.dropdown.Hit({(row.left+row.right)/2,(row.top+row.bottom)/2})==i,"popup row geometry matches hit testing");}
                ChooseToolbarDropdown(state,-1);expect(!state.dropdown.Open(),"popup cancellation closes the menu");
            }
            const std::filesystem::path folder=argv[1];std::filesystem::create_directories(folder);
            {
                auto canvas=MakeFrame({0,0,640,550},0xfffafafa);for(int y=0;y<550;++y)for(int x=320;x<640;++x)canvas.pixels[static_cast<size_t>(y)*640+x]=0xff141414;
                constexpr uint32_t colors[]={0xffeb2f96,0xfff5222d,0xfffa541c,0xfffa8c16,0xfffaad14,0xffa0d911,0xff52c41a,0xff13c2c2,0xff1677ff,0xff2f54eb,0xff722ed1};
                constexpr LPCWSTR names[]={L"magenta",L"red",L"volcano",L"orange",L"gold",L"lime",L"green",L"cyan",L"blue",L"geekblue",L"purple"};
                Document notes;for(int side=0;side<2;++side)for(int i=0;i<11;++i){Mark note;note.tool=Tool::Number;note.number_combo=NumberCombo::Text;note.number=i+1;note.number_size=24;note.number_text_preset=-2;note.number_text_size=16;note.color=colors[i];const float x=16.f+side*320,y=12.f+i*48;note.a={x,y};note.b={x+24,y+24};note.number_target={x+200,y+36};note.text=names[i];notes.Add(note);}
                Renderer renderer;SavePng(renderer.Flatten(canvas,notes,canvas.bounds),folder/"note-color-contrast.png");            }
            for(bool dark:{false,true}){Renderer renderer;const auto preview=renderer.Demo(true,dark,Tool::Pen,false,-1,-1,true);const auto path=folder/(dark?"highlighter-dark.png":"highlighter-light.png");SavePng(preview,path);expect(ReadPng(path).pixels==preview.pixels,"highlighter preview survives PNG export");}
            for(bool dark:{false,true}){Renderer renderer;SavePng(renderer.Demo(true,dark,Tool::Number,false,-1,70),folder/(dark?"number-text-dark.png":"number-text-light.png"));}
            state.tool=Tool::Number;state.number_combo=NumberCombo::Text;state.selected=true;state.busy=false;state.number_size=32;state.number_text_size=16;const auto badge_color=state.color;state.toolbar=PlaceToolbar({100,100,400,400},{0,0,1280,800},1,1,state.tool,state.number_combo);
            expect(!OpenToolbarDropdown(state,70,{0,0,1280,800}),"number label uses swatches instead of dropdown");state.number_text_color=*ToolbarColor(73);
            expect(state.number_text_color==*ToolbarColor(73)&&state.color==badge_color,"label color does not change badge color");
            AdjustPropertyValue(state,71,8);expect(state.number_text_size==24&&state.number_size==32,"label font size does not change badge size");
            {Frame canvas=MakeFrame({0,0,1200,540},0xffedf2f9);for(int y=0;y<540;++y)for(int x=0;x<1200;++x)if((x/12+y/12)%2==0)canvas.pixels[static_cast<size_t>(y)*1200+x]=0xffe4eaf4;Document combinations;
                for(int i=1;i<=6;++i){const float x=35.f+((i-1)%3)*395,y=35.f+((i-1)/3)*245;Mark badge;badge.tool=Tool::Number;badge.a={x,y+45};badge.b={x+42,y+87};badge.number_size=42;badge.number=i;badge.color=0xff0784ff;badge.number_shape=i==3?NumberShape::Pin:NumberShape::Circle;badge.number_combo=static_cast<NumberCombo>(i);badge.number_target={x+250,y+120};badge.text=L"这里是说明文字";combinations.Add(badge);
                    Mark label;label.tool=Tool::Text;label.a={x,y+170};label.b={x+340,y+210};label.font_size=19;label.text=NumberComboNames[i];label.color=0xff172b43;combinations.Add(label);}
                Renderer renderer;SavePng(renderer.Flatten(canvas,combinations,canvas.bounds),folder/"number-combinations.png");}
            {Frame canvas=MakeFrame({0,0,1000,280},0xffedf2f9);Document numbered;
                for(int i=0;i<10;++i){const float x=30.f+(i%5)*195,y=20.f+(i/5)*135;Mark badge;badge.tool=Tool::Number;badge.a={x+50,y};badge.b={x+(i==5?131:104),y+54};badge.number=i+1;badge.color=0xff0784ff;badge.number_shape=static_cast<NumberShape>(i);numbered.Add(badge);
                    Mark label;label.tool=Tool::Text;label.a={x+15,y+68};label.b={x+175,y+108};label.font_size=18;label.text=NumberShapeNames[i];label.color=0xff243142;label.text_align=TextAlign::Center;numbered.Add(label);}
                Renderer renderer;SavePng(renderer.Flatten(canvas,numbered,canvas.bounds),folder/"number-catalog.png");}
            {
                Frame canvas=MakeFrame({0,0,1320,1060},0xfff0f4fa);Document gallery;
                const auto label=[&](std::wstring text,Point a,Point b,float size){Mark mark;mark.tool=Tool::Text;mark.text=std::move(text);mark.a=a;mark.b=b;mark.font_size=size;mark.color=0xff243142;gallery.Add(mark);};
                for(int type=0;type<6;++type)label(ArrowTypeNames[type],{180.f+type*185,15},{360.f+type*185,50},18);
                for(int head=0;head<11;++head){const float y=70.f+head*88;label(ArrowHeadNames[head],{12,y},{178,y+42},16);
                    for(int type=0;type<6;++type){Mark arrow;arrow.tool=Tool::Arrow;arrow.a={190.f+type*185,y+32};arrow.b={330.f+type*185,y+15};arrow.width=3;arrow.arrow_size=23;arrow.color=0xff0784ff;arrow.arrow_type=static_cast<ArrowType>(type);arrow.arrow_head=static_cast<ArrowHead>(head);gallery.Add(arrow);}
                }
                Renderer renderer;SavePng(renderer.Flatten(canvas,gallery,canvas.bounds),folder/"arrow-catalog.png");
            }
            for(bool dark:{false,true})for(int id:{48,49,50,27,66}){
                Renderer renderer;const auto tool=id==27?Tool::Text:(id==48?Tool::Rectangle:Tool::Arrow);
                SavePng(renderer.Demo(true,dark,tool,false,-1,id),folder/(std::string(dark?"dropdown-dark-":"dropdown-light-")+std::to_string(id)+".png"));
            }
            for(bool dark:{false,true})for(int hover:{11,0,15,16}){
                Renderer renderer;auto frame=renderer.Demo(true,dark,Tool::Select,false,hover);
                SavePng(Crop(frame,{240,560,1120,670}),folder/(std::string(dark?"dark-tooltip-":"light-tooltip-")+std::to_string(hover)+".png"));
            }
            auto panel_gallery=MakeFrame({0,0,2016,1072},0xffeef1f5);
            for(bool dark:{false,true})for(Tool tool:{Tool::Select,Tool::Rectangle,Tool::Ellipse,Tool::Arrow,Tool::Pen,Tool::Text,Tool::Mosaic,Tool::Number}) {
                Renderer renderer;auto frame=renderer.Demo(true,dark,tool);
                const auto path=folder/(std::string(dark?"dark-":"light-")+std::to_string(static_cast<int>(tool))+".png");
                SavePng(frame,path);expect(ReadPng(path).pixels==frame.pixels,"context panel preview renders and roundtrips");
                const auto panel_bounds=PlaceToolbar({180,90,1110,600},frame.bounds,1,tool==Tool::Select?0.f:1.f,tool).bounds;
                const auto panel=Crop(frame,{LONG(panel_bounds.left)-2,LONG(panel_bounds.top)-2,LONG(panel_bounds.right)+2,LONG(panel_bounds.bottom)+2});
                const int gx=dark?1012:4,gy=static_cast<int>(tool)*134+4;
                for(int y=0;y<panel.Height();++y)for(int x=0;x<panel.Width();++x)panel_gallery.pixels[static_cast<size_t>(gy+y)*2016+gx+x]=panel.pixels[static_cast<size_t>(y)*panel.Width()+x];
                if(tool==Tool::Rectangle||tool==Tool::Select){const auto b=PlaceToolbar({180,90,1110,600},frame.bounds,1,tool==Tool::Select?0.f:1.f).bounds;SavePng(Crop(frame,{LONG(b.left)-2,LONG(b.top)-2,LONG(b.right)+2,LONG(b.bottom)+2}),folder/(tool==Tool::Select?(dark?"dark-compact.png":"light-compact.png"):(dark?"dark-panel.png":"light-panel.png")));}
            }
            SavePng(panel_gallery,folder/"all-toolbars.png");
        }
    }catch(const std::exception& e){std::cout<<"[FAIL] "<<e.what()<<'\n';++failures;}
    CoUninitialize();return failures?1:0;
}
