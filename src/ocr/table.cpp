#include "ocr/table.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <map>
namespace lumashot::ocr {
namespace {
struct Rule { int at{}, first{}, last{}; };
std::vector<Rule> Rules(const std::vector<uint8_t>& gray,int w,int h,bool vertical,int radius,int minimum) {
    const int across=vertical?w:h, along=vertical?h:w;
    std::vector<Rule> rules;
    auto sample=[&](int a,int b){return gray[static_cast<size_t>(vertical?b*w+a:a*w+b)];};
    for(int a=0;a<across;++a) {
        int first=-1,last=-1,hits=0,bestFirst=0,bestLast=0;
        for(int b=0;b<=along;++b) {
            bool on=false;
            if(b<along){const int value=sample(a,b);const int before=sample(std::max(0,a-radius),b),after=sample(std::min(across-1,a+radius),b);
                on=(value<245&&(a<radius||before-value>=6)&&(a+radius>=across||after-value>=6))||
                    (value>10&&(a<radius||value-before>=6)&&(a+radius>=across||value-after>=6));}
            if(on){if(first<0)first=b;last=b;++hits;}
            if(first>=0&&(b==along||b-last>2*radius+2)){
                if(last-first+1>bestLast-bestFirst&&hits*5>=(last-first+1)*3){bestFirst=first;bestLast=last+1;}
                first=-1;last=-1;hits=0;
            }
        }
        if(bestLast-bestFirst<minimum)continue;
        if(!rules.empty()&&a-rules.back().at<=3){rules.back().at=a;rules.back().first=std::min(rules.back().first,bestFirst);rules.back().last=std::max(rules.back().last,bestLast);}
        else rules.push_back({a,bestFirst,bestLast});
    }
    return rules;
}
bool ValidBox(Box b){return std::isfinite(b.left)&&std::isfinite(b.top)&&std::isfinite(b.right)&&std::isfinite(b.bottom)&&b.left<=b.right&&b.top<=b.bottom;}
std::optional<Table> Fill(const Text& text,const std::vector<float>& xs,const std::vector<float>& ys,bool ruled) {
    Table table{{xs.front(),ys.front(),xs.back(),ys.back()},static_cast<uint32_t>(ys.size()-1),static_cast<uint32_t>(xs.size()-1),{}};
    if(table.rows<2||table.columns<2||table.rows>256||table.columns>64)return {};
    table.cells.resize(static_cast<size_t>(table.rows)*table.columns);
    std::vector<const Line*> ordered;for(const auto& line:text.lines)ordered.push_back(&line);
    std::stable_sort(ordered.begin(),ordered.end(),[](const Line* a,const Line* b){return a->box.top<b->box.top;});
    for(const auto* line:ordered) {
        std::map<size_t,std::wstring> fragments;
        for(const auto& glyph:line->glyphs) {
            const auto b=glyph.box;if(!ValidBox(b))return {};
            const float x=(b.left+b.right)/2,y=(b.top+b.bottom)/2;
            if(x<xs.front()||x>=xs.back()||y<ys.front()||y>=ys.back())continue;
            const auto c=static_cast<size_t>(std::upper_bound(xs.begin(),xs.end(),x)-xs.begin()-1);
            const auto r=static_cast<size_t>(std::upper_bound(ys.begin(),ys.end(),y)-ys.begin()-1);
            // A glyph crossing a separator is ambiguous (including merged cells).
            if(b.left<xs[c]-2||b.right>xs[c+1]+2||b.top<ys[r]-2||b.bottom>ys[r+1]+2)return {};
            fragments[r*table.columns+c]+=glyph.text;
        }
        for(const auto& [i,value]:fragments)if(!value.empty()) {if(!table.cells[i].empty())table.cells[i]+=L'\n';table.cells[i]+=value;}
    }
    size_t populated=0;for(const auto& cell:table.cells)if(!cell.empty())++populated;
    if(populated<4||(!ruled&&populated*4<table.cells.size()*3))return {};
    return table;
}
}
std::wstring TableTsv(const Table& table) {
    if(!table.rows||!table.columns||table.cells.size()!=static_cast<size_t>(table.rows)*table.columns)return {};
    std::wstring result;
    for(uint32_t r=0;r<table.rows;++r){if(r)result+=L"\r\n";for(uint32_t c=0;c<table.columns;++c){if(c)result+=L'\t';
        const auto& value=table.cells[static_cast<size_t>(r)*table.columns+c];
        for(size_t i=0;i<value.size();++i){const auto ch=value[i];result+=(ch==L'\r'||ch==L'\n'||ch==L'\t')?L' ':ch;if(ch==L'\r'&&i+1<value.size()&&value[i+1]==L'\n')++i;}}}
    return result;
}
std::optional<Table> AnalyzeTable(const Frame& image,const Text& text) {
    if(image.Width()<=0||image.Height()<=0||text.lines.empty()||text.lines.size()>10000||image.pixels.size()!=static_cast<size_t>(image.Width())*image.Height())return {};
    // Min pooling preserves thin pale rules and bounds scratch memory to 1536 squared.
    const int scale=std::max(1,(std::max(image.Width(),image.Height())+1535)/1536);
    const int w=(image.Width()+scale-1)/scale,h=(image.Height()+scale-1)/scale;
    std::vector<uint8_t> ink(static_cast<size_t>(w)*h,255);
    for(int y=0;y<image.Height();++y)for(int x=0;x<image.Width();++x){const auto p=image.pixels[static_cast<size_t>(y)*image.Width()+x];
        const auto light=(((p>>16)&255)*77+((p>>8)&255)*150+(p&255)*29)>>8;
        auto& pixel=ink[static_cast<size_t>(y/scale)*w+x/scale];pixel=std::min(pixel,static_cast<uint8_t>(light));}
        float glyphHeight=0;for(const auto& line:text.lines){if(!ValidBox(line.box))return {};glyphHeight+=std::max(0.f,line.box.bottom-line.box.top);}
    glyphHeight/=static_cast<float>(text.lines.size());
    const int minimum=std::max(24,static_cast<int>(glyphHeight*3/static_cast<float>(scale)));
    const int radius=std::max(2,8/scale);
    auto vertical=Rules(ink,w,h,true,radius,minimum),horizontal=Rules(ink,w,h,false,radius,minimum);
    if(vertical.size()>=3&&horizontal.size()>=3&&vertical.size()<=65&&horizontal.size()<=257) {
        const int left=vertical.front().at,right=vertical.back().at,top=horizontal.front().at,bottom=horizontal.back().at;
        bool regular=true;
        for(const auto& v:vertical)if(v.first>top+radius+3||v.last<bottom-radius-3)regular=false;
        for(const auto& v:horizontal)if(v.first>left+radius+3||v.last<right-radius-3)regular=false;
        if(regular){std::vector<float> xs,ys;for(auto v:vertical)xs.push_back(static_cast<float>(v.at*scale));for(auto v:horizontal)ys.push_back(static_cast<float>(v.at*scale));return Fill(text,xs,ys,true);}
        return {}; // Partial dividers can be merged cells; do not infer them as borderless.
    }
    if(!vertical.empty()||horizontal.size()>1)return {};
    // Borderless fallback requires a complete, consistently aligned rectangular matrix.
    std::vector<const Line*> lines;float height=0;
    for(const auto& line:text.lines){if(!ValidBox(line.box)||line.glyphs.empty())return {};lines.push_back(&line);height+=line.box.bottom-line.box.top;}
    height/=static_cast<float>(lines.size());if(height<2)return {};
    std::sort(lines.begin(),lines.end(),[](const Line* a,const Line* b){return a->box.top<b->box.top;});
    std::vector<std::vector<const Line*>> rows;
    for(auto line:lines){if(rows.empty()||std::abs(line->box.top-rows.back().front()->box.top)>height*.45f)rows.push_back({});rows.back().push_back(line);}
    if(rows.size()<3||rows.size()>256)return {};
    for(auto& row:rows)std::sort(row.begin(),row.end(),[](const Line* a,const Line* b){return a->box.left<b->box.left;});
    const auto reference=std::max_element(rows.begin(),rows.end(),[](const auto& a,const auto& b){return a.size()<b.size();});
    const size_t cols=reference->size();if(cols<2||cols>64)return {};
    const auto anchors=*reference;
    std::vector<std::vector<const Line*>> matrix(rows.size(),std::vector<const Line*>(cols));
    for(size_t r=0;r<rows.size();++r){if(rows[r].size()<2)return {};for(auto line:rows[r]){
        size_t match=cols;for(size_t c=0;c<cols;++c)if(std::abs(line->box.left-anchors[c]->box.left)<=std::max(3.f,height*.3f)){if(match!=cols)return {};match=c;}
        if(match==cols||matrix[r][match])return {};matrix[r][match]=line;}}
    std::vector<float> xs(cols+1),ys(rows.size()+1);float right=0;
    for(size_t c=0;c<cols;++c){const float edge=anchors[c]->box.left;float maxRight=0;size_t evidence=0;
        for(const auto& row:matrix)if(row[c]){maxRight=std::max(maxRight,row[c]->box.right);++evidence;}
        if(evidence<2)return {};
        if(c+1<cols&&anchors[c+1]->box.left-maxRight<height*1.5f)return {};
        xs[c]=c?(right+edge)/2:edge-1;right=maxRight;}
    xs.back()=right+1;
    // Two-column lists are ambiguous unless every row contains numeric data.
    if(cols==2)for(const auto& row:matrix){bool numeric=false;if(!row[1])return {};for(const auto& g:row[1]->glyphs)for(auto ch:g.text)if(ch>=L'0'&&ch<=L'9')numeric=true;if(!numeric)return {};}
    for(size_t r=0;r<rows.size();++r){float bottom=0;for(auto line:rows[r])bottom=std::max(bottom,line->box.bottom);
        if(r+1<rows.size()&&rows[r+1][0]->box.top-bottom<height*.35f)return {};
        ys[r]=r?(ys[r]+rows[r][0]->box.top)/2:rows[r][0]->box.top-1;ys[r+1]=bottom;}    ys.back()+=1;xs.front()=std::max(0.f,xs.front());ys.front()=std::max(0.f,ys.front());xs.back()=std::min(static_cast<float>(image.Width()),xs.back());ys.back()=std::min(static_cast<float>(image.Height()),ys.back());return Fill(text,xs,ys,false);
}
}






