#include "app/tool_preferences.h"
#include "ui/render.h"
#include <iostream>
namespace lumashot {
int Test(){
    const auto path=std::filesystem::temp_directory_path()/(L"LumaShot-tools-test-"+std::to_wstring(GetCurrentProcessId())+L".ini");
    struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code error;std::filesystem::remove(path,error);}}cleanup{path};
    int failures=0;const auto expect=[&](bool ok,const char* name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';if(!ok)++failures;};
    expect(LoadToolProperties(path)==ToolProperties{},"missing configuration uses defaults");
    // Old LastTool keys must not affect style loading or default UI selection.
    WritePrivateProfileStringW(L"General",L"LastTool",L"pen",path.c_str());
    ToolProperties source;
    source.pen_polyline=true;source.pen_snap=false;source.pen_straight=true;source.color=0xff123456;source.width=7.5f;source.pen_opacity=.1234567f;source.pen_mode=PenMode::Highlighter;
    source.highlighter_color=0xffabcdef;source.highlighter_width=37;source.highlighter_opacity=.2345678f;source.pen_smoothing=PenSmoothing::High;
    source.line_style=LineStyle::DashDot;source.arrow_style=ArrowStyle::Double;source.arrow_type=ArrowType::HandDrawn;source.arrow_head=ArrowHead::Hollow;source.arrow_size=41;
    source.font_size=37;source.font_family=L"Microsoft YaHei";source.number_size=55;source.number_shape=NumberShape::Hexagon;source.number_combo=NumberCombo::Text;
    source.number_text_color=0xffabcdef;source.number_text_preset=4;source.number_text_size=29;source.text_bold=true;source.text_align=TextAlign::Right;source.text_background=TextBackground::Yellow;
    source.mosaic_cell=25;source.mosaic_brush=87;source.mosaic_strength=45;source.mosaic_mode=MosaicMode::Blur;source.mosaic_method=MosaicMethod::Rectangle;
    source.fill_color=0xffdecbab;source.fill_opacity=.4567891f;source.corner_radius=27;
    WritePrivateProfileStringW(L"General",L"Hotkey",L"65",path.c_str());
    SaveToolProperties(path,source);
    const auto restored=LoadToolProperties(path);expect(restored==source,"all tool properties round trip including fractional values and fill");
    expect(GetPrivateProfileIntW(L"General",L"Hotkey",0,path.c_str())==65,"saving properties preserves general preferences");
    ViewState next;static_cast<ToolProperties&>(next)=restored;
    expect(next.tool==Tool::Select&&!next.selected&&!next.picker.open&&!next.dropdown.Open()&&static_cast<const ToolProperties&>(next)==source,"new session restores only properties without interaction state");
    source.fill_color.reset();SaveToolProperties(path,source);expect(!LoadToolProperties(path).fill_color,"no fill remains disabled after restart");
    for(auto mode:{TextBackground::ToneLight,TextBackground::ToneDark,TextBackground::Automatic,TextBackground::TagAutomatic,TextBackground::TagLight,TextBackground::TagDark}){source.text_background=mode;SaveToolProperties(path,source);expect(LoadToolProperties(path).text_background==mode,"generated text backgrounds persist without reverting to legacy presets");}
    WritePrivateProfileStringW(L"Tools",L"tag_style_version",nullptr,path.c_str());
    for(const auto pair:{std::pair{L"4",TextBackground::TagLight},std::pair{L"5",TextBackground::TagDark},std::pair{L"6",TextBackground::TagAutomatic},std::pair{L"1",TextBackground::White}}){
        WritePrivateProfileStringW(L"Tools",L"text_background",pair.first,path.c_str());expect(LoadToolProperties(path).text_background==pair.second,"unversioned toolbar tone migrates once; legacy fixed colors survive");
    }
    SaveToolProperties(path,source);
    for(const auto key:{L"arrow_type",L"width",L"font_size",L"color",L"text_bold",L"fill_color"})WritePrivateProfileStringW(L"Tools",key,L"nan",path.c_str());
    WritePrivateProfileStringW(L"Tools",L"arrow_head",L"999",path.c_str());WritePrivateProfileStringW(L"Tools",L"number_combo",L"-1",path.c_str());
    WritePrivateProfileStringW(L"Tools",L"mosaic_brush",L"1e200",path.c_str());WritePrivateProfileStringW(L"Tools",L"text_align",L"1.5",path.c_str());
    WritePrivateProfileStringW(L"Tools",L"pen_straight",L"2",path.c_str());
    WritePrivateProfileStringW(L"Tools",L"pen_polyline",L"2",path.c_str());WritePrivateProfileStringW(L"Tools",L"pen_snap",L"2",path.c_str());
    const auto invalid=LoadToolProperties(path);const ToolProperties defaults;
    expect(!invalid.pen_straight&&!invalid.pen_polyline&&invalid.pen_snap&&invalid.arrow_type==defaults.arrow_type&&invalid.width==defaults.width&&invalid.font_size==defaults.font_size&&invalid.color==defaults.color&&!invalid.text_bold&&!invalid.fill_color&&invalid.arrow_head==defaults.arrow_head&&invalid.number_combo==defaults.number_combo&&invalid.mosaic_brush==defaults.mosaic_brush&&invalid.text_align==defaults.text_align,"invalid numbers enum ranges and nonfinite values fall back safely");
    expect(invalid.highlighter_width==source.highlighter_width&&invalid.font_family==source.font_family,"invalid fields do not discard other settings");
    return failures?1:0;
}
}
int main(){return lumashot::Test();}
