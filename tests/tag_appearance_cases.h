#pragma once
#include "model/tag_appearance.h"
#include "model/mark_properties.h"
namespace lumashot {
template<class Expect> void TagAppearanceCases(Expect expect){
    const auto pixelsNear=[](uint32_t a,uint32_t b){for(int shift:{0,8,16})if(std::abs(int((a>>shift)&255)-int((b>>shift)&255))>1)return false;return (a>>24)==(b>>24);};
    for(auto hue:AnnotationColors)for(auto backdrop:{0xffffffffu,0xff141414u,0xff808080u}){
        auto frame=MakeFrame({-200,-100,500,400},backdrop);Mark mark;mark.tool=Tool::Text;mark.a={-100,-40};mark.b={200,40};mark.color=hue;mark.text_background=TextBackground::TagAutomatic;
        const auto c=ResolveTextAppearance(frame,mark);
        expect(c.background&&(*c.background>>24)>=31&&(*c.background>>24)<=46,"tag fill is real 12/18 percent alpha");
        expect((c.ink>>24)==255&&(c.border>>24)<100,"ink stays opaque while tag border is subtle");
        expect(NoteContrast(NoteLuminance(c.ink),NoteLuminance(TagComposite(*c.background,backdrop)))>=4.5,"automatic tag ink meets contrast on actual uniform composite including mid-gray");
        Document document;document.Add(mark);Renderer renderer;const auto image=renderer.Flatten(frame,document,frame.bounds);
        expect(pixelsNear(image.pixels[size_t(100)*frame.Width()+200],TagComposite(*c.background,backdrop)),"real renderer alpha matches source-over math");
        expect(image.pixels[size_t(10)*frame.Width()+10]==backdrop,"tag fill and border do not affect outside pixels");
        mark.tool=Tool::Number;mark.a={-150,-40};mark.b={-110,0};mark.number_combo=NumberCombo::Text;mark.number_detail=Box{-100,-40,200,40};mark.text=L" ";
        const auto note=ResolveNoteAppearance(frame,mark);document.marks={mark};const auto noteImage=renderer.Flatten(frame,document,frame.bounds);
        expect(pixelsNear(noteImage.pixels[size_t(100)*frame.Width()+200],TagComposite(*note.background,backdrop)),"note background is filled once, not double tinted");
        mark.number_text_preset=-1;mark.number_text_color=0xff808080;
        const auto custom=ResolveNoteAppearance(frame,mark);
        expect(custom.ink==0xff808080&&(*custom.background>>24)<=46,"custom ink is exact without an opaque black/white fallback");
        mark.tool=Tool::Text;mark.text_background=TextBackground::TagLight;
        expect((*ResolveTextAppearance(frame,mark).background>>24)==31,"manual light tag keeps 12 percent alpha");
        mark.text_background=TextBackground::TagDark;
        expect((*ResolveTextAppearance(frame,mark).background>>24)==46,"manual dark tag keeps 18 percent alpha");
    }
    for(float scale:{1.f,1.5f,2.f})for(auto align:{TextAlign::Left,TextAlign::Center,TextAlign::Right}){
        Mark mark;mark.tool=Tool::Text;mark.text=L"Tag 标签\n第二行";mark.a={100,100};mark.b={500,230};mark.font_size=24*scale;mark.text_auto_size=true;mark.text_wrap_width=300*scale;mark.text_align=align;
        FitTextBounds(mark);const auto plain=Normalize(mark.a,mark.b);mark.text_background=TextBackground::TagAutomatic;FitTextBounds(mark);
        const auto tagged=Normalize(mark.a,mark.b);const auto padding=TextTagMetrics(mark.font_size);
        expect(std::abs(tagged.right-tagged.left-(plain.right-plain.left)-2*padding.x)<.1f&&std::abs(tagged.bottom-tagged.top-(plain.bottom-plain.top)-2*padding.y)<.1f,"multiline auto bounds include DPI-scaled tag padding exactly once");
        const auto saved=mark;FitTextBounds(mark);expect(mark==saved,"repeated tag measurement does not grow or drift");
        Document doc;doc.Add(mark);expect(doc.HitTest({tagged.left+2,tagged.top+2})==0,"tag padding belongs to selectable geometry");
        mark.text_auto_size=false;auto properties=PropertiesOfMark(mark,scale,ToolProperties{});properties.text_background=TextBackground::None;
        const auto manual=WithMarkProperties(mark,properties,scale);expect(manual.a==mark.a&&manual.b==mark.b,"manual box size is preserved when toggling tag mode");
    }
    // High-frequency texture remains visible; do not claim universal contrast.
    auto frame=MakeFrame({0,0,200,100},0xff101010);for(int y=0;y<100;++y)for(int x=0;x<200;++x)if((x/10+y/10)%2)frame.pixels[size_t(y)*200+x]=0xffeeeeee;
    Mark mark;mark.tool=Tool::Text;mark.a={10,10};mark.b={190,90};mark.text_background=TextBackground::TagAutomatic;Document doc;doc.Add(mark);Renderer renderer;
    const auto c=ResolveTextAppearance(frame,mark);const auto image=renderer.Flatten(frame,doc,frame.bounds);
    expect(pixelsNear(image.pixels[50*200+50],TagComposite(*c.background,frame.pixels[50*200+50]))&&pixelsNear(image.pixels[50*200+60],TagComposite(*c.background,frame.pixels[50*200+60]))&&image.pixels[50*200+50]!=image.pixels[50*200+60],"checker texture survives below the actual rendered tag");
    mark.text_background=TextBackground::ToneLight;expect((*ResolveTextAppearance(frame,mark).background>>24)==255,"legacy tonal style remains readable and opaque");
    mark.text_background=TextBackground::White;expect(ResolveTextAppearance(frame,mark).background==0xffffffff,"legacy white enum is not reinterpreted");
    // Sampling follows the rotated mark, not its unrotated screen rectangle.
    auto halves=MakeFrame({-100,-100,300,300},0xffffffff);for(int y=0;y<400;++y)for(int x=200;x<400;++x)halves.pixels[size_t(y)*400+x]=0xff141414;
    Mark note;note.tool=Tool::Number;note.a={80,80};note.b={120,120};note.number_combo=NumberCombo::Text;note.number_detail=Box{150,80,250,120};
    const auto first=ResolveNoteAppearance(halves,note);note.rotation=180;const auto second=ResolveNoteAppearance(halves,note);
    expect(first.background!=second.background,"rotated detail samples its actual backdrop");
}
}
