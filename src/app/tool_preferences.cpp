#include "app/tool_preferences.h"
#include "model/number_label.h"
#include <cmath>
#include <cwchar>
#include <stdexcept>
#include <type_traits>
#include <sstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <array>
#include <string_view>
namespace lumashot {
namespace {
template<class T,class Visitor> void Visit(T& value,Visitor visit){
    visit(L"color",value.color,0.0,4294967295.0);
    visit(L"width",value.width,1.0,16.0);
    visit(L"pen_opacity",value.pen_opacity,0.0,1.0);
    visit(L"pen_mode",value.pen_mode,0.0,1.0);
    visit(L"pen_straight",value.pen_straight,0.0,1.0);
    visit(L"pen_polyline",value.pen_polyline,0.0,1.0);
    visit(L"pen_snap",value.pen_snap,0.0,1.0);
    visit(L"highlighter_color",value.highlighter_color,0.0,4294967295.0);
    visit(L"highlighter_width",value.highlighter_width,1.0,64.0);
    visit(L"highlighter_opacity",value.highlighter_opacity,0.0,1.0);
    visit(L"pen_smoothing",value.pen_smoothing,0.0,2.0);
    visit(L"line_style",value.line_style,0.0,3.0);
    visit(L"arrow_style",value.arrow_style,0.0,2.0);
    visit(L"arrow_type",value.arrow_type,0.0,5.0);
    visit(L"arrow_head",value.arrow_head,0.0,10.0);
    visit(L"arrow_size",value.arrow_size,4.0,64.0);
    visit(L"font_size",value.font_size,8.0,96.0);
    visit(L"number_size",value.number_size,16.0,96.0);
    visit(L"number_shape",value.number_shape,0.0,9.0);
    visit(L"number_combo",value.number_combo,0.0,6.0);
    visit(L"number_text_color",value.number_text_color,0.0,4294967295.0);
    visit(L"number_text_preset",value.number_text_preset,-2.0,6.0);
    visit(L"number_text_size",value.number_text_size,8.0,64.0);
    visit(L"text_bold",value.text_bold,0.0,1.0);
    visit(L"text_align",value.text_align,0.0,2.0);
    visit(L"text_background",value.text_background,0.0,9.0);
    visit(L"mosaic_cell",value.mosaic_cell,4.0,64.0);
    visit(L"mosaic_brush",value.mosaic_brush,4.0,128.0);
    visit(L"mosaic_strength",value.mosaic_strength,1.0,100.0);
    visit(L"mosaic_mode",value.mosaic_mode,0.0,1.0);
    visit(L"mosaic_method",value.mosaic_method,0.0,1.0);
    visit(L"fill_opacity",value.fill_opacity,0.0,1.0);
    visit(L"corner_radius",value.corner_radius,0.0,32.0);
}
std::wstring Read(const std::filesystem::path& path,LPCWSTR key){
    wchar_t text[1024]{};GetPrivateProfileStringW(L"Tools",key,L"",text,1024,path.c_str());return text;
}
}
ToolProperties LoadToolProperties(const std::filesystem::path& path,ToolProperties value){
    if(path.empty())return value;
    Visit(value,[&](LPCWSTR key,auto& field,double minimum,double maximum){
        const auto text=Read(path,key);if(text.empty())return;
        wchar_t* end{};const double number=std::wcstod(text.c_str(),&end);
        using T=std::remove_reference_t<decltype(field)>;
        if(end==text.c_str()||*end||!std::isfinite(number)||number<minimum||number>maximum)return;
        if constexpr(!std::is_floating_point_v<T>)if(std::trunc(number)!=number)return;
        field=static_cast<T>(number);
    });
    if(Read(path,L"tag_style_version").empty()){
        if(value.text_background==TextBackground::Automatic)value.text_background=TextBackground::TagAutomatic;
        else if(value.text_background==TextBackground::ToneLight)value.text_background=TextBackground::TagLight;
        else if(value.text_background==TextBackground::ToneDark)value.text_background=TextBackground::TagDark;
    }
    const auto encoded=Read(path,L"number_label_hex");std::wstring label;bool valid=encoded.size()<=NumberLabelLimit*4&&encoded.size()%4==0;
    if(valid)for(size_t i=0;i<encoded.size();i+=4){unsigned value16=0;for(size_t j=0;j<4;++j){const wchar_t c=encoded[i+j];const int digit=c>=L'0'&&c<=L'9'?c-L'0':c>=L'a'&&c<=L'f'?c-L'a'+10:c>=L'A'&&c<=L'F'?c-L'A'+10:-1;if(digit<0){valid=false;break;}value16=value16*16+digit;}if(!valid)break;label+=static_cast<wchar_t>(value16);}
    value.number_label=valid?NormalizeNumberLabel(label):L"";
    const auto font=Read(path,L"font_family");
    if(!font.empty()&&font.size()<=128&&font.find_first_of(L"\r\n\t")==std::wstring::npos)value.font_family=font;
    const auto fill=Read(path,L"fill_color");
    if(!fill.empty()&&fill!=L"none"){
        wchar_t* end{};const double number=std::wcstod(fill.c_str(),&end);
        if(end!=fill.c_str()&&!*end&&std::isfinite(number)&&number>=0&&number<=4294967295.0&&std::trunc(number)==number)value.fill_color=static_cast<uint32_t>(number);
    }
    if(value.pen_polyline)value.pen_straight=true;
    return value;
}
void SaveToolProperties(const std::filesystem::path& path,const ToolProperties& tools){
    if(path.empty())return;
    std::error_code error;std::filesystem::create_directories(path.parent_path(),error);
    if(error)throw std::system_error(error,"Create tool preferences directory");
    std::wstring section;
    const auto append=[&](LPCWSTR key,const std::wstring& text){section+=key;section+=L'=';section+=text;section+=L'\0';};
    append(L"tag_style_version",L"1");
    Visit(tools,[&](LPCWSTR key,const auto& field,double,double){std::wostringstream text;text.imbue(std::locale::classic());text<<std::setprecision(std::numeric_limits<double>::max_digits10)<<static_cast<double>(field);append(key,text.str());});
    std::wstring encoded;constexpr wchar_t hex[]=L"0123456789abcdef";for(wchar_t c:NormalizeNumberLabel(tools.number_label))for(int shift=12;shift>=0;shift-=4)encoded+=hex[(static_cast<unsigned>(c)>>shift)&15];append(L"number_label_hex",encoded);
    append(L"font_family",tools.font_family);append(L"fill_color",tools.fill_color?std::to_wstring(*tools.fill_color):L"none");
    section+=L'\0';
    if(!WritePrivateProfileSectionW(L"Tools",section.c_str(),path.c_str()))throw std::runtime_error("Unable to save tool properties");
}
}
