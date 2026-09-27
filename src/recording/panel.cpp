#include "recording/panel.h"
#include "ui/controls.h"
#include "ui/glass_surface.h"
#include "ui/text_renderer.h"
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
namespace lumashot::recording {
using Microsoft::WRL::ComPtr;
SIZE PanelSize(const PanelState& s){if(s.state==State::Ready){const LONG extra=s.notice.empty()?0:26;return s.gif?SIZE{456,184+extra}:SIZE{544,208+extra};}if(s.state==State::Preview)return s.gif?SIZE{560,526}:SIZE{640,550};if(s.state==State::Failed)return {500,208};return s.gif?SIZE{316,48}:SIZE{380,48};}
namespace {
std::wstring Time(long long ticks){const auto seconds=std::max(0LL,ticks/10000000);return (seconds/60<10?L"0":L"")+std::to_wstring(seconds/60)+L":"+(seconds%60<10?L"0":L"")+std::to_wstring(seconds%60);}
struct Painter {
    ID2D1RenderTarget* t;IDWriteFactory* fonts;const PanelState& s;ComPtr<ID2D1SolidColorBrush> brush;std::vector<PanelButton> buttons;
    TextRenderer text_renderer;UINT32 ink,muted,border;Painter(ID2D1RenderTarget* target,IDWriteFactory* f,const PanelState& state):t(target),fonts(f),s(state),ink(ui::Theme{1,s.dark}.Ink()&0xffffff),muted(ui::Theme{1,s.dark}.Muted()&0xffffff),border(ui::Theme{1,s.dark}.Border()&0xffffff){t->CreateSolidColorBrush(D2D1::ColorF(ink),&brush);}
    void Color(UINT32 c,float a=1){brush->SetColor(D2D1::ColorF(c,a));}
    void Round(D2D1_RECT_F b,float r,UINT32 c,float a=1){Color(c,a);t->FillRoundedRectangle(D2D1::RoundedRect(b,r,r),brush.Get());}
    void Stroke(D2D1_RECT_F b,float r,UINT32 c,float a=.6f,float w=1){Color(c,a);t->DrawRoundedRectangle(D2D1::RoundedRect(b,r,r),brush.Get(),w);}
    void Line(float x,float y,float ex,float ey,UINT32 c,float width=1,float a=1){Color(c,a);t->DrawLine({x,y},{ex,ey},brush.Get(),width);}
    void Label(const std::wstring& text,D2D1_RECT_F b,float size,UINT32 c,bool center=false,bool bold=false){ComPtr<IDWriteTextFormat> f;fonts->CreateTextFormat(ui::ToolFont,nullptr,bold?DWRITE_FONT_WEIGHT_SEMI_BOLD:DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"zh-CN",&f);f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);if(center)f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);Color(c);text_renderer.Draw(t,fonts,text,f.Get(),b,D2D1::ColorF(c));}
    float Hot(int id)const{return s.interaction.ready?s.interaction.hover[size_t(id)]:(s.hover==id?1.f:0.f);}
    float Down(int id)const{return s.interaction.ready?s.interaction.press[size_t(id)]:0.f;}
    float Selected(int id,bool on)const{return s.interaction.ready?s.interaction.selected[size_t(id)]:(on?1.f:0.f);}
    static UINT32 Mix(UINT32 a,UINT32 b,float value){
        UINT32 result=0;for(int shift:{0,8,16})result|=UINT32(std::lround(float((a>>shift)&255)+(float((b>>shift)&255)-float((a>>shift)&255))*value))<<shift;
        return result;
    }
    void Feedback(int id,D2D1_RECT_F b,bool primary=false,bool selected=false,bool framed=false){
        const float hot=Hot(id),down=Down(id),choice=Selected(id,selected);
        if(primary)Round(b,7,0x0784ff);
        else if(choice>0)Round(b,7,0x64acff,.20f*choice);
        else if(framed)Round(b,7,s.dark?0x3e526e:0xffffff,s.dark?.10f:.22f);
        if(hot>0)Round(b,7,s.dark?0x9fc8ff:0xffffff,hot*(primary?.10f:s.dark?.14f:.36f));
        if(down>0)Round(b,7,0x246ebc,down*.22f);
        if(primary||framed||choice>0||hot>0)Stroke(b,7,choice>0?0x92c4ff:border,choice>0?.40f:framed?.28f:.18f*hot);
    }
    void Hit(int id,D2D1_RECT_F b){buttons.push_back({id,b});}
    void Icon(int icon,float x,float y,UINT32 c){Color(c);constexpr float w=1.65f;
        if(icon==0){Line(x-4,y-4,x+4,y+4,c,w);Line(x+4,y-4,x-4,y+4,c,w);}
        if(icon==1){Round({x-5,y-6,x-2,y+6},1,c);Round({x+2,y-6,x+5,y+6},1,c);}
        if(icon==2){Round({x-5,y-5,x+5,y+5},1.5f,c);}
        if(icon==3){Line(x-4,y-7,x-4,y+7,c,2);Line(x-4,y-7,x+7,y,c,2);Line(x-4,y+7,x+7,y,c,2);}
        if(icon==4){Line(x-5,y-6,x-5,y+5,c,w);Line(x-5,y-6,x+5,y+1,c,w);Line(x-5,y+5,x-1,y+2,c,w);Line(x-1,y+2,x+2,y+7,c,w);Line(x+2,y+7,x+4,y+6,c,w);Line(x+4,y+6,x+1,y+1,c,w);Line(x+1,y+1,x+5,y+1,c,w);}
        if(icon==5){Round({x-3,y-7,x+3,y+2},3,c);t->DrawRoundedRectangle(D2D1::RoundedRect({x-6,y-4,x+6,y+5},5,5),brush.Get(),w);Line(x,y+5,x,y+8,c,w);Line(x-3,y+8,x+3,y+8,c,w);}
        if(icon==6){Line(x-6,y-3,x-3,y-3,c,w);Line(x-6,y-3,x-6,y+3,c,w);Line(x-6,y+3,x-3,y+3,c,w);Line(x-3,y-3,x+1,y-6,c,w);Line(x-3,y+3,x+1,y+6,c,w);Line(x+1,y-6,x+1,y+6,c,w);Line(x+5,y-4,x+7,y,c,w);Line(x+7,y,x+5,y+4,c,w);}
        if(icon==7){Line(x-3,y-1,x,y+2,c,w);Line(x,y+2,x+3,y-1,c,w);}
        if(icon==8){for(int i=-1;i<=1;++i){Color(c);t->FillEllipse(D2D1::Ellipse({x+i*5.f,y},1,1),brush.Get());}}
    }
    void Button(int id,D2D1_RECT_F b,const std::wstring& label,bool primary=false,bool selected=false,bool down=false){
        Hit(id,b);ui::Control control;control.id=id;control.kind=down?ui::Kind::Dropdown:ui::Kind::Button;control.bounds={b.left,b.top,b.right,b.bottom};control.text=label;control.primary=primary;control.selected=selected;
        ui::PaintContext p;p.target=t;p.brush=[&](uint32_t c){Color(c&0xffffff,float(c>>24)/255);return brush.Get();};
        p.text=[&](const std::wstring& text,Box box,float size,uint32_t c){ComPtr<IDWriteTextFormat> f;fonts->CreateTextFormat(ui::ToolFont,nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"zh-CN",&f);f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);Color(c&0xffffff);text_renderer.Draw(t,fonts,text,f.Get(),{box.left,box.top,box.right,box.bottom},D2D1::ColorF(c&0xffffff));};
        p.measure_text=[&](const std::wstring& text,float size){ComPtr<IDWriteTextFormat> f;fonts->CreateTextFormat(ui::ToolFont,nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"zh-CN",&f);ComPtr<IDWriteTextLayout> layout;fonts->CreateTextLayout(text.c_str(),UINT32(text.size()),f.Get(),1000,100,&layout);DWRITE_TEXT_METRICS metrics{};layout->GetMetrics(&metrics);return Point{metrics.width,metrics.height};};
        Feedback(id,b,primary,selected,down);control.indicator_backed=true;
        ui::DrawControls(p,{1,s.dark},std::span(&control,1),-1);
    }
    void IconButton(int id,D2D1_RECT_F b,int icon,UINT32 color){
        Hit(id,b);Feedback(id,b);const float hot=Hot(id),down=Down(id);
        const D2D1_POINT_2F center{(b.left+b.right)/2,(b.top+b.bottom)/2};
        const float zoom=1+.05f*hot-.08f*down;D2D1_MATRIX_3X2_F original;t->GetTransform(&original);
        t->SetTransform(D2D1::Matrix3x2F::Scale(zoom,zoom,center)*D2D1::Matrix3x2F::Translation(0,-.7f*hot+.5f*down)*original);
        Icon(icon,center.x,center.y,color);t->SetTransform(original);
    }
    void Switch(int id,float x,float y,const std::wstring& label,int icon,bool on,float width){
        Hit(id,{x,y,x+width,y+28});const float value=Selected(id,on),hot=Hot(id);
        if(hot>0)Round({x-3,y,x+width+2,y+28},7,0x64acff,.10f*hot);
        Icon(icon,x+8,y+14,Mix(muted,ink,value));Label(label,{x+23,y,x+width-34,y+28},11,ink);
        Round({x+width-29,y+6,x+width-1,y+22},8,Mix(s.dark?0x42516a:0xc7ced9,0x0784ff,value));
        Color(0xffffff);t->FillEllipse(D2D1::Ellipse({x+width-21+12*value,y+14},6-.5f*Down(id),6-.5f*Down(id)),brush.Get());
    }
    void Image(const PreviewPixels& p,D2D1_RECT_F b,bool fit=true){if(p.pixels.empty()||!p.width||!p.height)return;const float ratio=fit?std::min((b.right-b.left)/p.width,(b.bottom-b.top)/p.height):0;const float cx=(b.left+b.right)/2,cy=(b.top+b.bottom)/2;if(fit)b={cx-p.width*ratio/2,cy-p.height*ratio/2,cx+p.width*ratio/2,cy+p.height*ratio/2};ComPtr<ID2D1Bitmap> bitmap;const auto properties=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE));if(SUCCEEDED(t->CreateBitmap(D2D1::SizeU(p.width,p.height),p.pixels.data(),p.width*4,properties,&bitmap)))t->DrawBitmap(bitmap.Get(),b,1,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);}
};
}
std::vector<PanelButton> DrawPanel(ID2D1RenderTarget* target,IDWriteFactory* fonts,const PanelState& s){
    Painter p(target,fonts,s);const auto size=PanelSize(s);const float width=float(size.cx),height=float(size.cy);
    const wchar_t* quality=s.gif?(s.gif_preset<0?L"自定义":s.gif_preset==0?L"小体积":s.gif_preset==2?L"清晰优先":L"均衡"):(s.quality==0?L"小体积":s.quality==2?L"高画质":L"均衡");
    target->Clear(D2D1::ColorF(0,0.f));ui::DrawGlassSurface(target,p.brush.Get(),{0,0,width,height},s.dark,s.acrylic);
    if(s.state==State::Ready){
        const float left=(width-246)/2;p.Round({left,10,left+246,36},13,s.dark?0x1c2837:0xccdcec,.22f);
        const auto& motion=s.tab_motion;
        if(motion.ready){const auto b=motion.indicator;p.Round({left+b.left,b.top,left+b.right,b.bottom},13,0x0784ff);}
        for(int i=0;i<3;++i){
            const bool selected=i==(s.gif?1:2);const D2D1_RECT_F b{left+i*82,10,left+(i+1)*82,36};
            p.Hit(i==0?40:i==1?1:2,b);
            if(!motion.ready&&selected)p.Round(b,13,0x0784ff);
            if(motion.ready){
                if(motion.hover[size_t(i+1)]>0)p.Round(b,13,0xffffff,motion.hover[size_t(i+1)]*(s.dark?.10f:.18f));
                if(motion.press[size_t(i+1)]>0)p.Round(b,13,0x102e50,motion.press[size_t(i+1)]*.16f);
            }
            const float center=(i+.5f)*82;
            const bool backed=motion.ready?center>=motion.indicator.left&&center<=motion.indicator.right:selected;
            p.Label(i==0?L"截图":i==1?L"GIF":L"录屏",b,11,backed?0xffffff:p.ink,true);
        }
        p.IconButton(90,{width-29,10,width-9,34},0,p.muted);
        if(s.gif){
            p.Button(15,{12,52,180,84},L"画质 · "+std::wstring(quality),false,false,true);
            p.Button(12,{188,52,306,84},std::to_wstring(s.fps)+L" 帧/秒",false,false,true);
            p.Button(13,{314,52,444,84},s.width?std::to_wstring(s.width)+L" px":L"原始尺寸",false,false,true);
            p.Switch(7,14,97,L"鼠标",4,s.cursor,100);
            p.Button(6,{126,94,214,126},L"重新选择");p.IconButton(10,{222,96,250,124},8,p.muted);
            p.Button(11,{266,94,444,126},s.selected?L"开始录制":L"选择区域",true);
            p.Label(L"GIF 不含声音",{14,130,width-14,151},10,p.muted);
            p.Label(s.size_estimate,{14,154,width-14,178},10,p.muted);
        }else{
            p.Button(3,{12,51,70,81},L"区域",false,s.source==0);p.Button(4,{74,51,132,81},L"窗口",false,s.source==1);p.Button(5,{136,51,194,81},L"全屏",false,s.source==2);
            p.Button(10,{434,51,532,81},L"MP4",false,false,true);
            p.Button(15,{12,91,212,121},L"画质 · "+std::wstring(quality),false,false,true);
            p.Button(12,{220,91,362,121},std::to_wstring(s.fps)+L" 帧/秒",false,false,true);
            p.Button(13,{370,91,532,121},s.width?std::to_wstring(s.width)+L" px":L"原始尺寸",false,false,true);
            p.Switch(8,14,135,L"系统声音",6,s.system,119);p.Switch(9,146,135,L"麦克风",5,s.microphone,105);p.Switch(7,264,135,L"鼠标",4,s.cursor,92);
            p.Button(11,{382,132,532,166},s.selected?L"开始录制":L"选择区域",true);
            p.Label(s.size_estimate,{14,178,width-14,202},10,p.muted);
        }
        if(!s.notice.empty())p.Label(s.notice,{14,height-28,width-14,height-6},10,p.muted,true);
    }else if(s.state==State::Preview){
        p.Color(0x0784ff);target->DrawEllipse(D2D1::Ellipse({23,23},6,6),p.brush.Get(),1.5f);target->FillEllipse(D2D1::Ellipse({23,23},2,2),p.brush.Get());p.Label(s.gif?L"GIF 预览":L"录制预览",{39,8,width-80,37},13,p.ink);p.IconButton(10,{width-73,9,width-45,36},8,p.muted);p.IconButton(90,{width-37,9,width-11,36},0,p.muted);
        const D2D1_RECT_F poster{16,48,width-16,s.gif?306.f:366.f};p.Round(poster,8,s.dark?0x101a26:0xd4e1ef);if(s.images){const auto& image=s.images->poster;const SIZE source=s.media_size.cx>0&&s.media_size.cy>0?s.media_size:SIZE{image.width,image.height};p.Image(image,PreviewImageBounds(poster,source,s.preview_scale),false);}p.Stroke(poster,8,p.border,.45f);
        if(s.play_pending||!s.images||(!s.detail.empty()&&!s.playing)){const auto label=!s.detail.empty()?s.detail.c_str():s.play_pending?L"正在准备播放…":s.preview_loading?L"正在加载预览…":L"封面暂不可用，仍可播放或保存";p.Label(label,{poster.left+16,poster.bottom-42,poster.right-16,poster.bottom-12},11,p.muted,true);}
        if(!s.playing){const auto x=width/2,y=(poster.top+poster.bottom)/2;p.Color(s.dark?0x182536:0x52687d,.85f);target->FillEllipse(D2D1::Ellipse({x,y},24+1.5f*p.Hot(22)-2*p.Down(22),24+1.5f*p.Hot(22)-2*p.Down(22)),p.brush.Get());p.Icon(3,x+1,y,0xffffff);}p.Hit(22,poster);
        if(s.gif){const D2D1_RECT_F strip{16,319,width-16,361};p.Round(strip,5,s.dark?0x182536:0xd2e3f5);const float tw=(width-32)/8;for(int i=0;i<8;++i){const D2D1_RECT_F b{18+i*tw,321,16+(i+1)*tw-2,359};if(s.images&&!s.images->thumbnails.empty())p.Image(s.images->thumbnails[std::min(size_t(i),s.images->thumbnails.size()-1)],b);else p.Round(b,2,s.dark?0x33485e:0xe6f0fa);}
            const float begin=s.time?16+(width-32)*float(double(s.trim_begin)/s.time):16,end=s.time?16+(width-32)*float(double(s.trim_end)/s.time):width-16;p.Stroke({begin,319,end,361},3,0x0784ff,1,2);p.Round({begin-4,319,begin+5,361},3,0x0784ff);p.Round({end-5,319,end+4,361},3,0x0784ff);p.Line(begin,331,begin,349,0xffffff,1.4f);p.Line(end,331,end,349,0xffffff,1.4f);p.IconButton(22,{16,365,44,392},s.playing?1:3,p.ink);p.Label(Time(s.trim_begin)+L" — "+Time(s.trim_end),{16,366,width-16,387},10,p.muted,true);
            p.Button(15,{16,395,140,425},quality,false,false,true);p.Button(13,{148,395,260,425},s.width?std::to_wstring(s.width)+L" px":L"原始尺寸",false,false,true);p.Button(12,{268,395,376,425},std::to_wstring(s.fps)+L" 帧/秒",false,false,true);p.Switch(14,390,396,L"循环播放",-1,s.loop,150);
            p.Label(s.size_estimate,{16,434,width-16,458},10,p.muted);
            p.Button(25,{16,479,188,512},L"重新录制");p.Button(23,{204,479,width-16,512},L"导出 GIF",true);
        }else{
            p.Round({12,374,width-12,456},10,s.dark?0x172535:0xf4f8fd,.74f);
            p.Stroke({12,374,width-12,456},10,p.border,.32f);
            const float y=389,played=16+(width-32)*float(s.time?std::clamp(double(s.position)/s.time,0.,1.):0.);
            p.Round({16,y-2,width-16,y+2},2,s.dark?0x3e5269:0xccdced);
            if(played>16)p.Round({16,y-2,played,y+2},2,0x0784ff);
            p.Color(s.dark?0xafd5ff:0xffffff);target->FillEllipse(D2D1::Ellipse({played,y},4.5f,4.5f),p.brush.Get());
            p.Hit(26,{16,377,width-16,402});
            p.IconButton(22,{20,402,48,430},s.playing?1:3,p.ink);
            p.Round({56,406,98,426},5,s.dark?0x2a4867:0xe0edfb);p.Label(L"MP4",{56,406,98,426},10,s.dark?0x9fceff:0x276cb3,true,true);
            const auto dimensions=s.media_size.cx>0&&s.media_size.cy>0?std::to_wstring(s.media_size.cx)+L" × "+std::to_wstring(s.media_size.cy):(s.width?std::to_wstring(s.width)+L" px":L"原始尺寸");
            p.Label(dimensions+L" · "+std::to_wstring(s.fps)+L" 帧/秒",{108,404,320,429},11,p.ink);
            p.Round({328,406,400,426},5,s.dark?0x29413c:0xe1f1e9);p.Label(quality,{328,406,400,426},10,s.dark?0x9dd7bf:0x34735a,true);
            p.Label(Time(s.position)+L" / "+Time(s.time),{width-145,404,width-24,429},11,p.ink);
            p.Label(s.size_estimate,{24,430,width-24,453},10,p.muted);
            p.Button(25,{16,467,214,501},L"重新录制");p.Button(23,{228,467,width-16,501},L"保存视频",true);
            p.Hit(24,{width-125,510,width-16,537});p.Label(L"另存为 GIF",{width-125,510,width-16,537},11,0x39a2ff,true);
        }
        if(!s.notice.empty()&&!s.exporting){
            const float x=width-276;p.Round({x,10,width-88,37},13,s.dark?0x203e40:0xe2f3ed,.98f);
            const UINT32 success=s.dark?0x81ddba:0x218263;
            p.Line(x+12,23,x+16,27,success,1.8f);p.Line(x+16,27,x+23,19,success,1.8f);
            p.Label(s.notice,{x+32,10,width-98,37},11,success);
        }
        if(s.exporting){p.Round({width-240,height-112,width-14,height-16},8,s.dark?0x213247:0xf7fbff,.98f);p.Stroke({width-240,height-112,width-14,height-16},8,p.border);p.Label((s.export_stage.empty()?L"正在生成 "+std::wstring(s.gif?L"GIF":L"文件"):s.export_stage)+(s.progress<0?L"…":L" "+std::to_wstring(s.progress)+L"%"),{width-225,height-105,width-25,height-73},11,p.ink);p.Round({width-224,height-66,width-65,height-62},2,s.dark?0x4a5d73:0xcbddef);p.Round({width-224,height-66,width-224+159*std::clamp(s.progress,0,100)/100.f,height-62},2,0x0784ff);p.IconButton(31,{width-55,height-78,width-22,height-49},0,p.muted);}
    }else if(s.state==State::Failed){p.Label(L"无法完成录制",{18,12,width-40,46},15,p.ink);p.IconButton(90,{width-34,12,width-10,38},0,p.muted);p.Label(s.detail,{18,54,width-18,139},11,p.muted);p.Button(25,{18,158,width-18,192},L"返回录制设置",true);
    }else{
        for(int y=19;y<=29;y+=5)for(int x=12;x<=16;x+=4){p.Color(p.muted,.5f);target->FillEllipse(D2D1::Ellipse({float(x),float(y)},.8f,.8f),p.brush.Get());}
        p.Color(s.state==State::Paused?0xf0b320:0xff4d59);target->FillEllipse(D2D1::Ellipse({32,24},4.5f,4.5f),p.brush.Get());p.Label(s.state==State::Starting?(s.countdown?L"即将开始 "+std::to_wstring(s.countdown):L"正在准备"):s.state==State::Finishing?L"正在完成":s.state==State::Paused?L"已暂停":Time(s.time),{46,7,130,41},12,p.ink,false,true);
        if(s.gif){p.Round({121,14,152,34},4,s.dark?0x3a4c62:0xd8e6f5);p.Label(L"GIF",{121,14,152,34},9,p.ink,true);p.Line(165,13,165,35,p.muted,1,.25f);}else{p.Icon(6,140,24,s.system?p.ink:p.muted);p.Icon(5,177,24,s.microphone?p.ink:p.muted);if(!s.microphone)p.Line(170,17,184,31,p.muted,1.2f);p.Line(203,13,203,35,p.muted,1,.25f);}
        const float x=s.gif?174.f:218.f;if(s.state==State::Recording||s.state==State::Paused){p.IconButton(20,{x,6,x+36,42},s.state==State::Paused?3:1,p.ink);p.IconButton(21,{x+42,6,x+78,42},2,0xff4d59);}p.Line(width-48,13,width-48,35,p.muted,1,.25f);p.IconButton(90,{width-42,6,width-8,42},0,p.ink);
    }
    for(const auto& button:p.buttons)if(button.id==s.focus){
        auto b=button.box;b={b.left+2,b.top+2,b.right-2,b.bottom-2};p.Stroke(b,7,0x0784ff,.85f);
    }
    return p.buttons;
}
}




