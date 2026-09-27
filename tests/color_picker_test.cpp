#include "ui/render.h"
#include "export/png.h"
#include <iostream>
using namespace lumashot;
int main() {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int failures=0;
    auto check=[&](bool ok,const char* name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';if(!ok)++failures;};
    for(uint32_t c:{0xffff4d4fu,0xff0080ffu,0xff000000u,0xffffffffu,0xff818181u})check(FromHsv(ToHsv(c))==c,"RGB HSV roundtrip");
    uint32_t value=0;check(ParseColorHex(L"#fF4d4F",value)&&value==0xffff4d4f,"mixed case hex");
    check(!ParseColorHex(L"#GG1234",value)&&value==0xffff4d4f,"invalid hex preserves color");
    ColorPicker p;
    for(float scale:{1.f,1.5f,2.f}) {
        p.Open(value,{-180,700,-20,730},{-480,0,0,800},scale);
        check(p.bounds.left>=-480&&p.bounds.right<=0&&p.bounds.top>=0&&p.bounds.bottom<=800,"negative origin and DPI bounds");
        for(int id:{0,1,2,3,4,5,6,10,11,12,13,14,15,16,17}){auto b=p.Part(id);check(p.Hit({(b.left+b.right)/2,(b.top+b.bottom)/2})==id,"control hit region");}
    }
    p.Focus(3);for(wchar_t c:std::wstring(L"00FF80"))p.Type(c);check(p.color==0xff00ff80,"typed hex updates color");
    p.Focus(4);p.Type(L'9');p.Type(L'9');auto last=p.color;p.Type(L'9');check(p.invalid&&p.color==last,"invalid RGB retains valid color");
    p.Step(4,1000);check(((p.color>>16)&255)==255,"RGB spinner clamps");
    p.Set(0xff0000ff);p.Set(0xff808080);check(p.hsv.h==240,"gray preserves chosen hue");
    auto square=p.Part(0);p.Down({square.left,square.top});p.Move({square.right+100,square.bottom+100});check(p.color==0xff000000,"drag clamps outside square");
    p.Remember();p.Remember();check(p.recent.size()==7&&p.recent[0]==0xff000000&&p.recent[1]!=p.recent[0],"recent colors deduplicate");
    try{for(bool dark:{false,true}){Renderer renderer;auto frame=renderer.Demo(true,dark,Tool::Pen,true);SavePng(frame,dark?L"color-picker-dark.png":L"color-picker-light.png");check(!frame.pixels.empty(),"acrylic picker rendering");}}
    catch(const std::exception& e){std::cout<<e.what()<<'\n';++failures;}
    CoUninitialize();return failures?1:0;
}
