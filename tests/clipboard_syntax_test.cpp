#include "clipboard/syntax_highlight.h"
#include <psapi.h>
#include <iostream>
#include <chrono>
#include <algorithm>
using namespace lumashot::clipboard;
static int failures=0;
static void Expect(bool value,const char* message){std::cout<<(value?"PASS ":"FAIL ")<<message<<std::endl;failures+=!value;}
static CodeColor At(const HighlightResult& result,size_t position){for(const auto& span:result.spans)if(position>=span.start&&position<span.start+span.length)return span.color;return CodeColor::Text;}
static size_t Memory(){PROCESS_MEMORY_COUNTERS_EX memory{};GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));return memory.PrivateUsage;}
int main(){
    const auto baseline=Memory();
    const std::wstring cpp=L"namespace lumashot {\nconst wchar_t* s = L\"中文😀\"; // comment\n/* multi\nline */ auto raw=R\"tag(if false)tag\"; return nullptr; }";
    const auto result=HighlightCode(cpp);Expect(result.language==CodeLanguage::Cpp,"auto detects screenshot-style C++");
    Expect(At(result,0)==CodeColor::Keyword&&At(result,cpp.find(L"nullptr"))==CodeColor::Keyword,"Lexilla classifies C++ keywords");
    Expect(At(result,cpp.find(L"中文"))==CodeColor::String&&At(result,cpp.find(L"😀")+1)==CodeColor::String,"UTF-8 lexer offsets map back to UTF-16 including emoji");
    Expect(At(result,cpp.find(L"multi"))==CodeColor::Comment&&At(result,cpp.find(L"line */"))==CodeColor::Comment,"multiline comments retain lexical state");
    Expect(At(result,cpp.find(L"if false"))==CodeColor::String,"raw string contents do not become keywords");
    uint32_t end=0;for(const auto& span:result.spans){Expect(span.start==end,"spans cover contiguous original text");end+=span.length;}Expect(end==cpp.size(),"full original text covered without changing copy payload");
    struct Sample{CodeLanguage language;const wchar_t* text;const wchar_t* keyword;};
    const Sample samples[]={{CodeLanguage::Python,L"def example():\n    return 'hello' # note",L"return"},{CodeLanguage::JavaScript,L"const text = `hello`; console.log(text);",L"const"},{CodeLanguage::TypeScript,L"interface User { name: string; }",L"interface"},{CodeLanguage::Json,L"{\"enabled\": true, \"count\": 42}",L"true"},{CodeLanguage::Bash,L"#!/bin/bash\nif test -f file; then echo yes; fi",L"if"},{CodeLanguage::PowerShell,L"param($name)\nif ($name) { Write-Host 'hello' }",L"if"},{CodeLanguage::Sql,L"SELECT name FROM users WHERE id = 42;",L"SELECT"}};
    for(const auto& sample:samples){const std::wstring text=sample.text;const auto parsed=HighlightCode(text,sample.language);Expect(At(parsed,text.find(sample.keyword))==CodeColor::Keyword,"selected language uses real lexer keyword styles");Expect(HighlightCode(text).language==sample.language,"automatic language detection matches representative snippet");}
    const std::wstring cs=L"using System;\npublic class Example { public string Name { get; set; } = @\"中文😀\"; }";
    const auto csharp=HighlightCode(cs);Expect(csharp.language==CodeLanguage::CSharp&&At(csharp,cs.find(L"public"))==CodeColor::Keyword&&At(csharp,cs.find(L"中文"))==CodeColor::String,"C# auto detection, keywords and verbatim strings");
    const std::wstring md=L"# Title\n\n**strong** and [link](https://example.com)\n\n```cpp\nreturn 42;\n```\n";
    const auto markdown=HighlightCode(md);Expect(markdown.language==CodeLanguage::Markdown&&At(markdown,md.find(L"Title"))==CodeColor::Keyword&&At(markdown,md.find(L"return"))==CodeColor::String,"Markdown headings links and fenced code retain source styles");
    Expect(HighlightCode(L"请查看项目说明：https://example.com").language==CodeLanguage::Plain,"ordinary prose and URL stay plain text");
    Expect(HighlightCode(cpp,CodeLanguage::Plain).spans.size()==1,"manual plain text override disables all colouring");
    {HighlightWorker worker(nullptr);for(uint64_t i=1;i<=30;++i)worker.Request(i==30?L"{\"ok\": true}":cpp,CodeLanguage::Auto,i);bool latest=false;for(int i=0;i<2000&&!latest;++i){if(auto value=worker.Take()){Expect(value->generation==30,"superseded analysis cannot replace latest content");latest=value->generation==30&&value->language==CodeLanguage::Json;}Sleep(1);}Expect(latest,"bounded asynchronous worker returns newest request");}
    std::wstring large;while(large.size()<65000)large+=cpp+L"\n";large.resize(65000);
    const auto start=std::chrono::steady_clock::now();size_t peak=Memory();for(int i=0;i<100;++i){auto parsed=HighlightCode(large,CodeLanguage::Cpp);peak=std::max(peak,Memory());Expect(!parsed.spans.empty(),"large snippet remains bounded and analysable");}
    const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::cout<<"LEXILLA_BENCH chars="<<large.size()<<" mean_ms="<<elapsed/100<<" baseline_private="<<baseline<<" peak_private="<<peak<<" final_private="<<Memory()<<std::endl;
    Expect(peak<=baseline+10*1024*1024,"lexer-only private memory increase stays within 10 MiB budget");return failures?1:0;
}
