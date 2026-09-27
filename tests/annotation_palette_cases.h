#pragma once
#include "model/note_color.h"
#include "tag_appearance_cases.h"
#include "export/png.h"
#include <filesystem>
namespace lumashot {
template<class Expect> void AnnotationPaletteCases(Expect expect){
    TagAppearanceCases(expect);
    const auto contrast=[](uint32_t ink,uint32_t bg){return NoteContrast(NoteLuminance(ink),NoteLuminance(bg));};
    for(size_t i=0;i<AnnotationColors.size();++i){
        expect(ToolbarColor(AnnotationColorIds[i])==AnnotationColors[i],"stroke swatches share six hue families");
        expect(ToolbarColor(AnnotationFillIds[i])==AnnotationColors[i],"fill swatches share the same hue order");
        expect(ToolbarColor(72+int(i))==AnnotationColors[i],"note swatches share the same hue order");
        ViewState highlighter;highlighter.tool=Tool::Pen;highlighter.pen_mode=PenMode::Highlighter;
        expect(PenToolbarColor(AnnotationColorIds[i],highlighter)==AnnotationColors[i],"highlighter uses the same six presets");
    }
    for(uint32_t hue:{AnnotationColors[0],AnnotationColors[1],AnnotationColors[2],AnnotationColors[3],AnnotationColors[4],AnnotationColors[5],0xff000000u,0xffffffffu,0xff808080u,0xffffff00u})for(bool dark:{false,true}){
        const auto pair=AnnotationTones(hue,dark);expect(contrast(pair.ink,pair.background)>=4.5,"automatic foreground meets 4.5 contrast for light and dark backgrounds");
        auto frame=MakeFrame({-100,-50,500,350},dark?0xff141414:0xffffffff);
        Mark m;m.tool=Tool::Text;m.a={0,0};m.b={200,70};m.color=hue;m.text=L"配色 Aa";m.text_background=TextBackground::Automatic;
        auto appearance=ResolveTextAppearance(frame,m);expect(appearance.background&&appearance.ink==pair.ink&&*appearance.background==pair.background,"automatic text appearance samples the frame including negative origin");
        m.text_background=TextBackground::ToneLight;expect(ResolveTextAppearance(frame,m).ink==AnnotationTones(hue,false).ink,"explicit light tone ignores backdrop");
        m.text_background=TextBackground::ToneDark;expect(ResolveTextAppearance(frame,m).ink==AnnotationTones(hue,true).ink,"explicit dark tone ignores backdrop");
        m.text_background=TextBackground::None;appearance=ResolveTextAppearance(frame,m);expect(!appearance.background&&appearance.ink==hue,"unbacked custom text remains exact");
        m.text_background=TextBackground::White;expect(ResolveTextAppearance(frame,m).ink==hue,"legacy explicit text remains exact");
        m.tool=Tool::Number;m.number_combo=NumberCombo::Text;m.number_target={330,100};m.number_text_preset=-2;
        appearance=ResolveNoteAppearance(frame,m);expect(appearance.background&&contrast(appearance.ink,TagComposite(*appearance.background,dark?0xff141414u:0xffffffffu))>=4.5,"automatic number notes stay readable");
        m.number_text_preset=-1;m.number_text_color=0xffab1234;expect(ResolveNoteAppearance(frame,m).ink==m.number_text_color,"custom note ink is not replaced");
    }
    for(auto tool:{Tool::Rectangle,Tool::Ellipse,Tool::Arrow,Tool::Pen,Tool::Text,Tool::Number})for(int width:{400,800,1320}){
        ViewState state;state.selected=true;state.tool=tool;state.number_combo=NumberCombo::Text;
        state.toolbar=PlaceToolbar({10,10,float(width-10),700},{0,0,width,1000},1.5f,1,tool,NumberCombo::Text);
        Document document;const auto controls=ToolbarControls(state,document);int colors=0;
        for(const auto& c:controls){if(c.kind==ui::Kind::Swatch&&std::find(AnnotationColorIds.begin(),AnnotationColorIds.end(),c.id)!=AnnotationColorIds.end())++colors;
            if(c.id>=20)expect(c.bounds.left>=state.toolbar.bounds.left-.1f&&c.bounds.right<=state.toolbar.bounds.right+.1f,"property controls stay within narrow and high-DPI toolbar");}
        expect(colors==6,"every applicable tool exposes exactly six hue presets");
    }
    ViewState text;text.selected=true;text.tool=Tool::Text;text.toolbar=PlaceToolbar({20,20,800,600},{0,0,1200,900},1,1,Tool::Text);
    expect(OpenToolbarDropdown(text,52,{0,0,1200,900}),"text background menu opens");
    expect(text.dropdown.items.size()==6,"background menu includes translucent tags and explicit enhanced solid options");
    ChooseToolbarDropdown(text,1);expect(text.text_background==TextBackground::TagAutomatic,"automatic background option commits");
    const std::filesystem::path output=L"build/tag-preview";std::filesystem::create_directories(output);
    {Renderer demo;SavePng(demo.Demo(true,false,Tool::Rectangle),output/L"toolbar.png");}
    for(int variant=0;variant<4;++variant){
        const bool dark=variant==1;
        auto frame=MakeFrame({0,0,840,560},dark?0xff141414:0xfffafafa);Document document;
        if(variant==2)for(int y=0;y<560;++y)for(int x=0;x<840;++x)frame.pixels[size_t(y)*840+x]=((x/18+y/18)%2)?0xffedf1f5:0xffd3dbe3;
        if(variant==3)for(int y=0;y<560;++y)for(int x=0;x<840;++x){const unsigned shade=unsigned(40+190*x/839);frame.pixels[size_t(y)*840+x]=0xff000000|shade*0x010101;}

        for(size_t i=0;i<AnnotationColors.size();++i){
            const float y=25+float(i)*86;Mark badge;badge.tool=Tool::Number;badge.color=AnnotationColors[i];badge.number=int(i)+1;badge.number_shape=NumberShape::Capsule;badge.number_size=44;badge.a={25,y};badge.b={85,y+44};document.marks.push_back(badge);
            Mark textMark;textMark.tool=Tool::Text;textMark.color=AnnotationColors[i];textMark.text=L"同色配色  Aa 123";textMark.font_size=25;textMark.a={115,y};textMark.b={430,y+48};textMark.text_background=TextBackground::TagAutomatic;textMark.text_auto_size=true;textMark.text_wrap_width=300;FitTextBounds(textMark);document.marks.push_back(textMark);
            Mark note=badge;note.a={490,y};note.b={550,y+44};note.number_combo=NumberCombo::Text;note.number_detail=Box{570,y,820,y+52};note.number_target={820,y+52};note.number_text_preset=-2;note.text=L"文字说明 / Note";note.number_text_size=20;document.marks.push_back(note);
        }
        Renderer renderer;SavePng(renderer.Flatten(frame,document,frame.bounds),output/(variant==0?L"light.png":variant==1?L"dark.png":variant==2?L"texture.png":L"gradient.png"));
    }
}
}
