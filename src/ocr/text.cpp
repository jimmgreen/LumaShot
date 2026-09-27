#include "ocr/text.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace lumashot::ocr {
std::wstring Utf16(const std::string& s) {
    if(s.empty())return {};
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(!n)throw std::runtime_error("Invalid UTF-8 in OCR data");
    std::wstring out(static_cast<size_t>(n),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n);return out;
}
std::wstring ErrorMessage(const std::exception& error) {
    try{return Utf16(error.what());}catch(...){return L"操作失败，请重试。";}
}
static bool Combining(const std::wstring& s) {
    const auto scalar=[&]{if(s.empty())return uint32_t(0);if(s.size()>1&&s[0]>=0xd800&&s[0]<=0xdbff&&s[1]>=0xdc00&&s[1]<=0xdfff)return 0x10000u+((uint32_t(s[0])-0xd800u)<<10)+(uint32_t(s[1])-0xdc00u);return uint32_t(s[0]);}();
    if((scalar>=0x1f3fb&&scalar<=0x1f3ff)||(scalar>=0xe0020&&scalar<=0xe007f))return true;
    WORD type{};
    return !s.empty() && ((GetStringTypeW(CT_CTYPE3,s.data(),1,&type) && (type&(C3_NONSPACING|C3_DIACRITIC|C3_VOWELMARK))) ||
        (s[0]>=0xfe00&&s[0]<=0xfe0f) || s[0]==0x200d);
}
Line DecodeCtc(std::span<const float> scores,size_t steps,size_t classes,
    const std::vector<std::wstring>& dict,Box box,float valid_ratio) {
    if(!steps||classes!=dict.size()+1||scores.size()!=steps*classes||valid_ratio<=0||valid_ratio>1)
        throw std::runtime_error("Unexpected OCR recognition output");
    struct Token {size_t id,start,end;float score;};std::vector<Token> tokens;
    size_t previous=0;
    for(size_t t=0;t<steps;++t) {
        const auto row=scores.subspan(t*classes,classes);
        const size_t id=static_cast<size_t>(std::max_element(row.begin(),row.end())-row.begin());
        if(id && id!=previous)tokens.push_back({id,t,t+1,row[id]});
        else if(id && !tokens.empty()){tokens.back().end=t+1;tokens.back().score=std::max(tokens.back().score,row[id]);}
        previous=id;
    }
    Line line;line.box=box;line.quad={Point{box.left,box.top},Point{box.right,box.top},Point{box.right,box.bottom},Point{box.left,box.bottom}};
    for(size_t i=0;i<tokens.size();++i) {
        const auto& token=tokens[i];
        const float start=i?float(tokens[i-1].end+token.start)*0.5f:0.0f;
        const float end=i+1<tokens.size()?float(token.end+tokens[i+1].start)*0.5f:float(steps)*valid_ratio;
        const auto x=[&](float v){return box.left+(box.right-box.left)*std::clamp(v/(float(steps)*valid_ratio),0.0f,1.0f);};
        Glyph g{dict[token.id-1],{x(start),box.top,x(end),box.bottom},token.score};
        if(!line.glyphs.empty()&&(Combining(g.text)||line.glyphs.back().text.back()==0x200d)) {
            line.glyphs.back().text+=g.text;line.glyphs.back().box.right=g.box.right;
        }else line.glyphs.push_back(std::move(g));
        line.confidence+=token.score;
    }
    if(!tokens.empty())line.confidence/=float(tokens.size());return line;
}
// Recursive whitespace cuts: separate columns before sorting lines top to bottom.
static void Order(std::vector<Line>& lines,size_t begin,size_t end) {
    if(end-begin<2)return;
    std::stable_sort(lines.begin()+begin,lines.begin()+end,[](const Line& a,const Line& b){return a.box.left<b.box.left;});
    float right=lines[begin].box.right;
    for(size_t i=begin+1;i<end;++i) {
        const float gap=lines[i].box.left-right;
        if(gap>std::max(12.0f,(lines[i].box.bottom-lines[i].box.top)*0.8f)) {
            Order(lines,begin,i);Order(lines,i,end);return;
        }
        right=std::max(right,lines[i].box.right);
    }
    std::stable_sort(lines.begin()+begin,lines.begin()+end,[](const Line& a,const Line& b){
        if(a.box.top!=b.box.top)return a.box.top<b.box.top;return a.box.left<b.box.left;
    });
}
void ReadingOrder(Text& text){Order(text.lines,0,text.lines.size());}
bool OnText(const Text& text,Point p){for(const auto& line:text.lines)for(const auto& g:line.glyphs)if(Contains(g.box,p))return true;return false;}
Position Hit(const Text& text,Point p,bool nearest) {
    Position best{};float distance=std::numeric_limits<float>::max();
    for(size_t l=0;l<text.lines.size();++l)for(size_t g=0;g<text.lines[l].glyphs.size();++g) {
        const auto b=text.lines[l].glyphs[g].box;
        const float dx=std::max({b.left-p.x,0.0f,p.x-b.right}),dy=std::max({b.top-p.y,0.0f,p.y-b.bottom});
        const float d=dx*dx+dy*dy*4;
        if(d<distance&&(nearest||Contains(b,p))) {distance=d;best={l,g+(p.x>(b.left+b.right)*0.5f?1u:0u)};}
    }return best;
}
Selection All(const Text& text){return text.lines.empty()?Selection{}:Selection{{0,0},{text.lines.size()-1,text.lines.back().glyphs.size()}};}
static int Category(const std::wstring& s) {
    const wchar_t c=s.empty()?0:s[0];
    if((c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||(c>=L'0'&&c<=L'9')||c==L'_')return 1;
    if(c>=0x3400&&c<=0x9fff)return 2;return 0;
}
Selection Word(const Text& text,Point p,bool entire) {
    for(size_t l=0;l<text.lines.size();++l){const auto& gs=text.lines[l].glyphs;
        for(size_t g=0;g<gs.size();++g)if(Contains(gs[g].box,p)) {
            if(entire)return {{l,0},{l,gs.size()}};
            size_t a=g,b=g+1;const int category=Category(gs[g].text);
            if(category){while(a&&Category(gs[a-1].text)==category)--a;while(b<gs.size()&&Category(gs[b].text)==category)++b;}
            return {{l,a},{l,b}};
        }
    }return {};
}
std::wstring Selected(const Text& text,Selection s) {
    if(s.caret<s.anchor)std::swap(s.anchor,s.caret);std::wstring out;
    for(size_t l=s.anchor.line;l<text.lines.size()&&l<=s.caret.line;++l) {
        const auto& gs=text.lines[l].glyphs;
        const size_t a=l==s.anchor.line?std::min(s.anchor.glyph,gs.size()):0;
        const size_t b=l==s.caret.line?std::min(s.caret.glyph,gs.size()):gs.size();
        if(l>s.anchor.line)out+=L"\r\n";for(size_t g=a;g<b;++g)out+=gs[g].text;
    }return out;
}
std::vector<Box> Highlights(const Text& text,Selection s) {
    if(s.caret<s.anchor)std::swap(s.anchor,s.caret);std::vector<Box> boxes;
    for(size_t l=s.anchor.line;l<text.lines.size()&&l<=s.caret.line;++l) {
        const auto& gs=text.lines[l].glyphs;
        for(size_t g=l==s.anchor.line?s.anchor.glyph:0;g<gs.size()&&(l!=s.caret.line||g<s.caret.glyph);++g)boxes.push_back(gs[g].box);
    }return boxes;
}
}
