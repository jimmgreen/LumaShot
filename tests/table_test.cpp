#include "ocr/table.h"
#include "ocr/protocol.h"
#include "ocr/engine.h"
#include "export/png.h"
#include <iostream>
#include <chrono>
#include <psapi.h>
#include <fstream>
#include <objbase.h>
using namespace lumashot;using namespace lumashot::ocr;
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
Line Cell(float x,float y,const std::wstring& value){Line l;l.box={x,y,x+static_cast<float>(value.size())*8,y+16};l.confidence=1;for(size_t i=0;i<value.size();++i)l.glyphs.push_back({value.substr(i,1),{x+static_cast<float>(i)*8,y,x+static_cast<float>(i+1)*8,y+16},1});return l;}
Text Matrix(){Text t;for(int r=0;r<3;++r)for(int c=0;c<3;++c)t.lines.push_back(Cell(40.f+c*200.f,40.f+r*80.f,L"Data  12"));return t;}
Frame Grid(int thick,uint32_t color){auto f=MakeFrame({0,0,680,300},0xffffffff);for(int i=0;i<4;++i){for(int k=0;k<thick;++k){for(int x=20;x<=620;++x)f.pixels[static_cast<size_t>(20+i*80+k)*680+x]=color;for(int y=20;y<=260;++y)f.pixels[static_cast<size_t>(y)*680+20+i*200+k]=color;}}return f;}
int main(int argc,char**) {CoInitializeEx(nullptr,COINIT_MULTITHREADED);try {
    auto text=Matrix();for(auto color:{0xff000000u,0xffeeeeeeu})for(int thick:{1,5}){auto t=AnalyzeTable(Grid(thick,color),text);Require(t&&t->rows==3&&t->columns==3,"ruled grid");Require(t->cells[0]==L"Data  12","internal spaces");}
    auto filled=Grid(1,0xff666666);for(int y=21;y<100;++y)for(int x=21;x<620;++x)if(x!=220&&x!=420)filled.pixels[static_cast<size_t>(y)*680+x]=0xffdaeaf8;Require(AnalyzeTable(filled,text).has_value(),"blue filled header");
    auto dark=Grid(1,0xffcccccc);for(auto& p:dark.pixels)if(p==0xffffffff)p=0xff202020;Require(AnalyzeTable(dark,text).has_value(),"dark background light rules");
    auto blank=MakeFrame({0,0,680,300},0xffffffff);Require(AnalyzeTable(blank,text).has_value(),"borderless matrix");
    Text list;for(int r=0;r<3;++r){list.lines.push_back(Cell(40,40.f+r*40,L"1."));list.lines.push_back(Cell(160,40.f+r*40,L"List entry"));}Require(!AnalyzeTable(blank,list),"numbered list rejected");
    auto surrounded=MakeFrame({0,0,2000,1200},0xffffffff);auto inset=Grid(1,0xff666666);for(int y=0;y<300;++y)std::copy_n(inset.pixels.begin()+static_cast<ptrdiff_t>(y)*680,680,surrounded.pixels.begin()+static_cast<ptrdiff_t>(y)*2000);Require(AnalyzeTable(surrounded,text).has_value(),"small primary table on large image");
    auto multiline=text;multiline.lines.push_back(Cell(40,62,L"next"));auto t=AnalyzeTable(Grid(1,0xff000000),multiline);Require(t&&t->cells[0]==L"Data  12\nnext","multiline cell");Require(TableTsv(*t).find(L"Data  12 next\t")==0,"TSV whitespace");
    auto missing=text;missing.lines[5]=Cell(440,120,L"Right  35");missing.lines.erase(missing.lines.begin()+4);
    for(const auto& image:{blank,Grid(1,0xff000000)}){auto emptyCell=AnalyzeTable(image,missing);Require(emptyCell&&emptyCell->rows==3&&emptyCell->columns==3&&emptyCell->cells.size()==9,"empty middle cell dimensions");Require(emptyCell->cells[4].empty()&&emptyCell->cells[5]==L"Right  35"&&emptyCell->cells[6]==L"Data  12","empty middle cell preserves following columns and rows");Require(TableTsv(*emptyCell).find(L"\r\nData  12\t\tRight  35\r\n")!=std::wstring::npos,"empty middle cell TSV position");}
    auto mixed=text;mixed.lines[0]=Cell(40,40,L"中文 ABC  123");mixed.lines[5]=Cell(440,120,L"金额  35.50");
    for(const auto& image:{blank,Grid(1,0xff000000)}){auto mixedTable=AnalyzeTable(image,mixed);Require(mixedTable&&mixedTable->cells[0]==L"中文 ABC  123"&&mixedTable->cells[5]==L"金额  35.50","mixed Chinese English numeric cells");Require(TableTsv(*mixedTable).find(L"中文 ABC  123\t")==0&&TableTsv(*mixedTable).find(L"\t金额  35.50\r\n")!=std::wstring::npos,"mixed content TSV preserves spaces");}
    Text paragraph;for(int r=0;r<4;++r)paragraph.lines.push_back(Cell(40,40.f+r*30,L"This is a paragraph."));Require(!AnalyzeTable(blank,paragraph),"paragraph rejected");
    auto merged=Grid(1,0xff000000);for(int y=21;y<100;++y)merged.pixels[static_cast<size_t>(y)*680+220]=0xffffffff;Require(!AnalyzeTable(merged,text),"merged grid rejected");
    text.table=t;auto encoded=Encode(text,L"");std::wstring error;auto decoded=Decode(encoded,error);Require(decoded.table&&TableTsv(*decoded.table)==TableTsv(*t),"protocol roundtrip");
    auto rejects=[](Bytes b){std::wstring e;try{Decode(b,e);return false;}catch(...){return true;}};
    encoded.position=0;encoded.data.pop_back();Require(rejects(encoded),"truncated reply");auto bad=Encode(text,L"");bad.data.push_back(0);Require(rejects(bad),"trailing reply");bad=Encode({},L"");bad.data.back()=2;Require(rejects(bad),"invalid table flag");
    auto invalid=text;invalid.lines[0].box.left=std::numeric_limits<float>::quiet_NaN();Require(rejects(Encode(invalid,L"")),"nonfinite OCR geometry");
    Bytes invalidTable;invalidTable.Put(kProtocol);invalidTable.PutString(L"");invalidTable.Put(uint32_t(0));invalidTable.Put(uint32_t(1));invalidTable.Put(Box{0,0,100,100});invalidTable.Put(uint32_t(257));invalidTable.Put(uint32_t(2));Require(rejects(invalidTable),"oversize table dimensions");
    invalidTable=Bytes{};invalidTable.Put(kProtocol);invalidTable.PutString(L"");invalidTable.Put(uint32_t(0));invalidTable.Put(uint32_t(1));invalidTable.Put(Box{0,0,100,100});invalidTable.Put(uint32_t(2));invalidTable.Put(uint32_t(2));Require(rejects(invalidTable),"missing table cells");
    auto large=MakeFrame({0,0,5440,2400},0xffffffff);auto fixture=Grid(1,0xffeeeeee);for(int y=0;y<2400;++y)for(int x=0;x<5440;++x)large.pixels[static_cast<size_t>(y)*5440+x]=fixture.pixels[static_cast<size_t>(y/8)*680+x/8];auto scaled=Matrix();for(auto& line:scaled.lines){line.box={line.box.left*8,line.box.top*8,line.box.right*8,line.box.bottom*8};for(auto& g:line.glyphs)g.box={g.box.left*8,g.box.top*8,g.box.right*8,g.box.bottom*8};}
    const auto start=std::chrono::steady_clock::now();Require(AnalyzeTable(large,scaled).has_value(),"scaled grid");const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();PROCESS_MEMORY_COUNTERS memory{};GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory));std::cout<<"large analysis ms="<<ms<<" peak working MB="<<memory.PeakWorkingSetSize/(1024*1024)<<"\n";
    if(argc>1){fixture=filled;DibSurface surface(680,300);std::copy(fixture.pixels.begin(),fixture.pixels.end(),surface.Pixels());HFONT font=CreateFontW(-24,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Arial");auto previous=SelectObject(surface.Dc(),font);SetBkMode(surface.Dc(),TRANSPARENT);SetTextColor(surface.Dc(),RGB(0,0,0));const wchar_t* values[]={L"Item",L"Count",L"Price",L"Apple",L"",L"35",L"Orange",L"24",L"60"};for(int r=0;r<3;++r)for(int c=0;c<3;++c)TextOutW(surface.Dc(),40+c*200,40+r*80,values[r*3+c],static_cast<int>(wcslen(values[r*3+c])));SelectObject(surface.Dc(),previous);DeleteObject(font);std::copy(surface.Pixels(),surface.Pixels()+fixture.pixels.size(),fixture.pixels.begin());SavePng(fixture,L"build/table-fixture.png");Engine engine(L"build/ocr");auto real=engine.Recognize(fixture);auto result=AnalyzeTable(fixture,real);Require(result&&result->rows==3&&result->columns==3,"real OCR table");Require(result->cells[4].empty()&&result->cells[5]==L"35"&&result->cells[6]==L"Orange","real OCR middle empty cell position");std::wcout<<TableTsv(*result)<<L"\n";Require(TableTsv(*result)==L"Item\tCount\tPrice\r\nApple\t\t35\r\nOrange\t24\t60","real OCR exact TSV");std::ofstream expected("build/table-expected.tsv",std::ios::binary),actual("build/table-actual.tsv",std::ios::binary);expected<<"Item\tCount\tPrice\r\nApple\t\t35\r\nOrange\t24\t60";for(auto ch:TableTsv(*result))actual.put(static_cast<char>(ch));}
    std::cout<<"table tests passed\n";CoUninitialize();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";CoUninitialize();return 1;}}







