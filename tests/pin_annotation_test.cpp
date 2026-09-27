#include "pin/annotation.h"
#include <iostream>
using namespace lumashot;
int main(){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int failures=0;
    auto expect=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';failures+=!ok;};
    try{
        auto image=MakeFrame({0,0,400,200},0xfffafafa);Document doc;Mark mark;mark.tool=Tool::Rectangle;mark.a={-80,120};mark.b={-20,150};mark.width=2;mark.fill_color=0xffff0000;mark.fill_opacity=1;doc.Add(mark);
        RECT display{-100,100,100,200};auto mapped=PinAnnotationDocument(doc,display,image);
        expect(mapped.marks[0].a==Point{40,40}&&mapped.marks[0].b==Point{160,100}&&mapped.marks[0].width==4,"zoomed display annotations map back to original pixels");
        Renderer renderer;auto flattened=renderer.Flatten(image,mapped,image.bounds);
        expect(flattened.Width()==400&&flattened.Height()==200&&flattened.pixels[60*400+60]!=image.pixels[60*400+60]&&flattened.pixels[0]==image.pixels[0],"annotation commits at source resolution with unchanged exterior");
        auto preview=PinAnnotationPreview(image,display,{-300,0,300,400});expect(preview.Width()==600&&preview.pixels[110*600+210]==0xfffafafa,"negative-monitor preview uses source image without desktop capture");
        Document empty;expect(renderer.Flatten(image,PinAnnotationDocument(empty,display,image),image.bounds).pixels==image.pixels,"empty annotation preserves every source pixel");
        Mark text;text.tool=Tool::Text;text.a={20,20};text.b={180,70};text.font_family=L"Consolas";text.text_auto_size=true;text.text_wrap_width=300;Document fonts;fonts.marks.push_back(text);
        const auto shown=PinAnnotationDisplayDocument(fonts,image,display);const auto restored=PinAnnotationDocument(shown,display,image);
        expect(shown.marks[0].text_wrap_width==150&&restored.marks[0].text_wrap_width==300&&restored.marks[0].font_family==text.font_family&&restored.marks[0].text_auto_size,"font and automatic wrapping limit survive pin zoom coordinate mapping");
    }catch(const std::exception& e){std::cout<<e.what()<<'\n';++failures;}
    CoUninitialize();return failures?1:0;
}
