#include "ui/color_picker.h"
#include <algorithm>
#include <cmath>
namespace lumashot {
Hsv ToHsv(uint32_t c) {
    const float r=float((c>>16)&255)/255,g=float((c>>8)&255)/255,b=float(c&255)/255;
    const float hi=std::max({r,g,b}),lo=std::min({r,g,b}),d=hi-lo;Hsv hsv{0,hi?d/hi:0,hi};
    if(d){if(hi==r)hsv.h=60*std::fmod((g-b)/d,6.0f);else if(hi==g)hsv.h=60*((b-r)/d+2);else hsv.h=60*((r-g)/d+4);if(hsv.h<0)hsv.h+=360;}return hsv;
}
uint32_t FromHsv(Hsv h) {
    h.h=std::fmod(std::max(0.0f,h.h),360.0f);h.s=std::clamp(h.s,0.0f,1.0f);h.v=std::clamp(h.v,0.0f,1.0f);
    const float c=h.v*h.s,x=c*(1-std::abs(std::fmod(h.h/60,2.0f)-1)),m=h.v-c;
    float r{},g{},b{};if(h.h<60){r=c;g=x;}else if(h.h<120){r=x;g=c;}else if(h.h<180){g=c;b=x;}else if(h.h<240){g=x;b=c;}else if(h.h<300){r=x;b=c;}else{r=c;b=x;}
    const auto byte=[&](float v){return static_cast<uint32_t>(std::lround((v+m)*255));};return 0xff000000|(byte(r)<<16)|(byte(g)<<8)|byte(b);
}
std::wstring ColorHex(uint32_t color){wchar_t text[8]{};swprintf_s(text,L"#%06X",color&0xffffff);return text;}
bool ParseColorHex(std::wstring text,uint32_t& color) {
    if(!text.empty()&&text.front()==L'#')text.erase(0,1);if(text.size()!=6)return false;
    uint32_t n=0;for(auto c:text){int digit=c>=L'0'&&c<=L'9'?c-L'0':(c>=L'a'&&c<=L'f'?c-L'a'+10:(c>=L'A'&&c<=L'F'?c-L'A'+10:-1));if(digit<0)return false;n=(n<<4)|static_cast<uint32_t>(digit);}color=0xff000000|n;return true;
}
void ColorPicker::Set(uint32_t value){color=value|0xff000000;const auto next=ToHsv(color);if(next.s>0)hsv.h=next.h;hsv.s=next.s;hsv.v=next.v;}
void ColorPicker::Open(uint32_t value,Box anchor,RECT monitor,float dpi_scale) {
    scale=std::min({dpi_scale,float(monitor.right-monitor.left-16)/288,float(monitor.bottom-monitor.top-16)/296});scale=std::max(0.25f,scale);
    const float w=288*scale,h=296*scale;
    const float left=std::clamp(anchor.right-w,float(monitor.left)+8,std::max(float(monitor.left)+8,float(monitor.right)-w-8));
    float top=anchor.top-h-10*scale;if(top<monitor.top+8)top=anchor.bottom+10*scale;
    top=std::clamp(top,float(monitor.top)+8,std::max(float(monitor.top)+8,float(monitor.bottom)-h-8));
    bounds={left,top,left+w,top+h};original=value;color=value;hsv=ToHsv(value);open=true;field=drag=-1;invalid=false;input.clear();
}
Box ColorPicker::Part(int id) const {
    Box b{};
    if(id==0)b={16,56,150,202};if(id==1)b={160,56,176,202};if(id==2)b={250,12,278,40};
    if(id==3)b={190,104,272,128};if(id>=4&&id<=6)b={210,136+float(id-4)*24,272,158+float(id-4)*24};
    if(id==7)b={190,54,272,92};
    if(id>=10&&id<=17)b={15+float(id-10)*33,250,39+float(id-10)*33,274};
    return {bounds.left+b.left*scale,bounds.top+b.top*scale,bounds.left+b.right*scale,bounds.top+b.bottom*scale};
}
int ColorPicker::Hit(Point p) const {for(int i:{0,1,2,3,4,5,6,10,11,12,13,14,15,16,17})if(Contains(Part(i),p))return i;return -1;}
void ColorPicker::Focus(int index){field=index;input=field==3?ColorHex(color):std::to_wstring((color>>((6-field)*8))&255);replace=true;invalid=false;}
bool ColorPicker::Commit() {
    if(field<3||field>6)return true;uint32_t value=color;bool valid=false;
    if(field==3)valid=ParseColorHex(input,value);
    else if(!input.empty()&&input.size()<=3){unsigned n=0;valid=true;for(auto c:input){if(c<L'0'||c>L'9'){valid=false;break;}n=n*10+unsigned(c-L'0');}valid=valid&&n<=255;if(valid){const int shift=(6-field)*8;value=(color&~(255u<<shift))|(n<<shift);}}
    invalid=!valid;if(valid)Set(value);return valid;
}
void ColorPicker::Type(wchar_t c) {
    if(field<3)return;if(c==8){if(replace)input.clear();else if(!input.empty())input.pop_back();replace=false;Commit();return;}
    const bool allowed=(c>=L'0'&&c<=L'9')||(field==3&&((c>=L'a'&&c<=L'f')||(c>=L'A'&&c<=L'F')||c==L'#'));
    if(!allowed)return;if(replace){input.clear();replace=false;}if(input.size()<(field==3?7u:3u))input+=c;Commit();
}
void ColorPicker::Step(int index,int amount){field=index;const int shift=(6-index)*8;if(index==3)Set(0xff000000|static_cast<uint32_t>(std::clamp(int(color&0xffffff)+amount,0,0xffffff)));else if(index>=4&&index<=6)Set((color&~(255u<<shift))|(static_cast<uint32_t>(std::clamp(int((color>>shift)&255)+amount,0,255))<<shift));Focus(index);}
void ColorPicker::Remember(){std::erase(recent,color);recent.insert(recent.begin(),color);if(recent.size()>7)recent.resize(7);}
void ColorPicker::Down(Point p) {
    const int id=Hit(p);if(field>=3)Commit();drag=-1;
    if(id>=3&&id<=6){const auto b=Part(id);if(p.x>b.right-14*scale)Step(id,p.y<(b.top+b.bottom)/2?1:-1);else Focus(id);return;}
    field=-1;invalid=false;
    if(id==0||id==1){drag=id;Move(p);}if(id>=10&&id<17&&size_t(id-10)<recent.size())Set(recent[id-10]);if(id==17)Remember();
}
void ColorPicker::Move(Point p) {
    if(drag<0)return;const auto b=Part(drag);
    if(drag==0){hsv.s=std::clamp((p.x-b.left)/(b.right-b.left),0.0f,1.0f);hsv.v=1-std::clamp((p.y-b.top)/(b.bottom-b.top),0.0f,1.0f);}
    if(drag==1)hsv.h=std::clamp((p.y-b.top)/(b.bottom-b.top),0.0f,1.0f)*359.999f;
    color=FromHsv(hsv);
}
}
