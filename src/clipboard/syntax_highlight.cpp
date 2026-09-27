#include "clipboard/syntax_highlight.h"
#include "clipboard/lex_document.h"
#include <LexerModule.h>
#include <SciLexer.h>
#include <memory>
#include <array>
#include <cwctype>
extern const Lexilla::LexerModule lmCPP,lmPython,lmJSON,lmBash,lmPowerShell,lmSQL,lmMarkdown;
namespace lumashot::clipboard {
const wchar_t* LanguageName(CodeLanguage language){static constexpr const wchar_t* names[]={L"自动",L"纯文本",L"C / C++",L"Python",L"JavaScript",L"TypeScript",L"JSON",L"Bash",L"PowerShell",L"SQL",L"C#",L"Markdown"};return names[static_cast<size_t>(language)];}
static CodeLanguage Detect(std::wstring_view text){
    const auto has=[&](std::wstring_view needle){return text.find(needle)!=text.npos;};
    const auto first=text.find_first_not_of(L" \t\r\n");if(first==text.npos)return CodeLanguage::Plain;
    if(text.substr(first).starts_with(L"# ")||has(L"\n## ")||has(L"```")||(has(L"](")&&has(L"["))||(has(L"**")&&has(L"\n")))return CodeLanguage::Markdown;
    if(has(L"using System")||has(L"Console.")||has(L"public class ")||has(L"public sealed ")||has(L"public record ")||has(L"Task<")||has(L"get; set;"))return CodeLanguage::CSharp;
    if((text[first]==L'{'||text[first]==L'[')&&has(L"\"")&&has(L":")&&!has(L";"))return CodeLanguage::Json;
    if(has(L"#include")||has(L"namespace ")||has(L"std::")||has(L"noexcept")||has(L"nullptr")||has(L"int main("))return CodeLanguage::Cpp;
    if(has(L"#!/bin/bash")||has(L"#!/bin/sh")||(has(L"then")&&has(L"fi"))||has(L"set -e"))return CodeLanguage::Bash;
    if(has(L"Write-Host")||has(L"Get-ChildItem")||has(L"$env:")||has(L"param("))return CodeLanguage::PowerShell;
    if(has(L"def ")||has(L"elif ")||(has(L"import ")&&!has(L";")&&!has(L" from ")&&!has(L"import {")&&!has(L"import \"" )&&!has(L"import '"))||(has(L"print(")&&has(L":")))return CodeLanguage::Python;
    if(has(L"interface ")||has(L": string")||has(L": number")||has(L"export type "))return CodeLanguage::TypeScript;
    if(has(L"import {")||has(L" from ")||has(L"function ")||has(L"console.")||has(L"=>")||has(L"const ")||has(L"let "))return CodeLanguage::JavaScript;
    std::wstring upper(text.substr(0,8192));for(auto& c:upper)c=static_cast<wchar_t>(towupper(c));
    if((upper.find(L"SELECT ")!=upper.npos&&upper.find(L" FROM ")!=upper.npos)||upper.find(L"CREATE TABLE")!=upper.npos||upper.find(L"INSERT INTO")!=upper.npos)return CodeLanguage::Sql;
    return CodeLanguage::Plain;
}
static CodeColor Classify(CodeLanguage language,int style){
    using C=CodeColor;
    if(language==CodeLanguage::Cpp||language==CodeLanguage::CSharp||language==CodeLanguage::JavaScript||language==CodeLanguage::TypeScript){style&=63;switch(style){case SCE_C_WORD:return C::Keyword;case SCE_C_WORD2:case SCE_C_GLOBALCLASS:return C::Type;case SCE_C_COMMENT:case SCE_C_COMMENTLINE:case SCE_C_COMMENTDOC:case SCE_C_COMMENTLINEDOC:case SCE_C_COMMENTDOCKEYWORD:case SCE_C_COMMENTDOCKEYWORDERROR:case SCE_C_PREPROCESSORCOMMENT:case SCE_C_PREPROCESSORCOMMENTDOC:return C::Comment;case SCE_C_STRING:case SCE_C_CHARACTER:case SCE_C_STRINGEOL:case SCE_C_VERBATIM:case SCE_C_REGEX:case SCE_C_STRINGRAW:case SCE_C_TRIPLEVERBATIM:case SCE_C_HASHQUOTEDSTRING:case SCE_C_ESCAPESEQUENCE:return C::String;case SCE_C_NUMBER:return C::Number;case SCE_C_PREPROCESSOR:return C::Preprocessor;case SCE_C_OPERATOR:return C::Operator;default:return C::Text;}}
    if(language==CodeLanguage::Markdown){switch(style){case SCE_MARKDOWN_HEADER1:case SCE_MARKDOWN_HEADER2:case SCE_MARKDOWN_HEADER3:case SCE_MARKDOWN_HEADER4:case SCE_MARKDOWN_HEADER5:case SCE_MARKDOWN_HEADER6:return C::Keyword;case SCE_MARKDOWN_LINK:return C::Type;case SCE_MARKDOWN_CODE:case SCE_MARKDOWN_CODE2:case SCE_MARKDOWN_CODEBK:return C::String;case SCE_MARKDOWN_STRONG1:case SCE_MARKDOWN_STRONG2:case SCE_MARKDOWN_EM1:case SCE_MARKDOWN_EM2:return C::Preprocessor;case SCE_MARKDOWN_ULIST_ITEM:case SCE_MARKDOWN_OLIST_ITEM:case SCE_MARKDOWN_BLOCKQUOTE:case SCE_MARKDOWN_HRULE:return C::Operator;default:return C::Text;}}
    if(language==CodeLanguage::Python){switch(style){case SCE_P_WORD:case SCE_P_WORD2:return C::Keyword;case SCE_P_COMMENTLINE:case SCE_P_COMMENTBLOCK:return C::Comment;case SCE_P_NUMBER:return C::Number;case SCE_P_CLASSNAME:case SCE_P_DEFNAME:case SCE_P_DECORATOR:return C::Type;case SCE_P_STRING:case SCE_P_CHARACTER:case SCE_P_TRIPLE:case SCE_P_TRIPLEDOUBLE:case SCE_P_STRINGEOL:case SCE_P_FSTRING:case SCE_P_FCHARACTER:case SCE_P_FTRIPLE:case SCE_P_FTRIPLEDOUBLE:return C::String;case SCE_P_OPERATOR:return C::Operator;default:return C::Text;}}
    if(language==CodeLanguage::Json){switch(style){case SCE_JSON_KEYWORD:case SCE_JSON_LDKEYWORD:return C::Keyword;case SCE_JSON_PROPERTYNAME:return C::Type;case SCE_JSON_NUMBER:return C::Number;case SCE_JSON_STRING:case SCE_JSON_STRINGEOL:case SCE_JSON_ESCAPESEQUENCE:case SCE_JSON_URI:case SCE_JSON_COMPACTIRI:return C::String;case SCE_JSON_LINECOMMENT:case SCE_JSON_BLOCKCOMMENT:return C::Comment;case SCE_JSON_OPERATOR:return C::Operator;default:return C::Text;}}
    if(language==CodeLanguage::Bash){switch(style){case SCE_SH_WORD:return C::Keyword;case SCE_SH_COMMENTLINE:return C::Comment;case SCE_SH_NUMBER:return C::Number;case SCE_SH_STRING:case SCE_SH_CHARACTER:case SCE_SH_BACKTICKS:case SCE_SH_HERE_Q:case SCE_SH_HERE_DELIM:return C::String;case SCE_SH_SCALAR:case SCE_SH_PARAM:return C::Type;case SCE_SH_OPERATOR:return C::Operator;default:return C::Text;}}
    if(language==CodeLanguage::PowerShell){switch(style){case SCE_POWERSHELL_KEYWORD:return C::Keyword;case SCE_POWERSHELL_COMMENT:case SCE_POWERSHELL_COMMENTSTREAM:case SCE_POWERSHELL_COMMENTDOCKEYWORD:return C::Comment;case SCE_POWERSHELL_STRING:case SCE_POWERSHELL_CHARACTER:case SCE_POWERSHELL_HERE_STRING:case SCE_POWERSHELL_HERE_CHARACTER:return C::String;case SCE_POWERSHELL_NUMBER:return C::Number;case SCE_POWERSHELL_VARIABLE:case SCE_POWERSHELL_CMDLET:case SCE_POWERSHELL_FUNCTION:return C::Type;case SCE_POWERSHELL_OPERATOR:return C::Operator;default:return C::Text;}}
    if(language==CodeLanguage::Sql){switch(style){case SCE_SQL_WORD:case SCE_SQL_WORD2:return C::Keyword;case SCE_SQL_COMMENT:case SCE_SQL_COMMENTLINE:case SCE_SQL_COMMENTDOC:case SCE_SQL_COMMENTLINEDOC:return C::Comment;case SCE_SQL_STRING:case SCE_SQL_CHARACTER:return C::String;case SCE_SQL_NUMBER:return C::Number;case SCE_SQL_OPERATOR:return C::Operator;default:return C::Text;}}
    return C::Text;
}
HighlightResult HighlightCode(std::wstring_view input,CodeLanguage language){
    const auto text=input.substr(0,65536);HighlightResult result;if(language==CodeLanguage::Auto)language=Detect(text);result.language=language;
    if(text.empty())return result;if(language==CodeLanguage::Plain){result.spans.push_back({0,static_cast<uint32_t>(text.size()),CodeColor::Text});return result;}
    const int count=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);std::string utf8(static_cast<size_t>(count),'\0');WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),utf8.data(),count,nullptr,nullptr);
    LexDocument document(std::move(utf8));const Lexilla::LexerModule* module=&lmCPP;
    if(language==CodeLanguage::Markdown)module=&lmMarkdown;else if(language==CodeLanguage::Python)module=&lmPython;else if(language==CodeLanguage::Json)module=&lmJSON;else if(language==CodeLanguage::Bash)module=&lmBash;else if(language==CodeLanguage::PowerShell)module=&lmPowerShell;else if(language==CodeLanguage::Sql)module=&lmSQL;
    const auto release=[](Scintilla::ILexer5* lexer){if(lexer)lexer->Release();};std::unique_ptr<Scintilla::ILexer5,decltype(release)> lexer(module->Create(),release);
    const char* words="";
    switch(language){
    case CodeLanguage::CSharp:words="abstract add alias and as ascending async await base bool break by byte case catch char checked class const continue decimal default delegate descending do double dynamic else enum equals event explicit extern false file finally fixed float for foreach from get global goto group if implicit in init int interface internal into is join let lock long managed nameof namespace new nint not notnull nuint null object on operator or orderby out override params partial private protected public readonly record ref remove required return sbyte scoped sealed select set short sizeof stackalloc static string struct switch this throw true try typeof uint ulong unchecked unmanaged unsafe ushort using value var virtual void volatile when where while with yield";lexer->PropertySet("lexer.cpp.triplequoted.strings","1");break;
    case CodeLanguage::Cpp:words="alignas alignof and asm auto bool break case catch char char8_t char16_t char32_t class concept const consteval constexpr constinit const_cast continue co_await co_return co_yield decltype default delete do double dynamic_cast else enum explicit export extern false float for friend if inline int long mutable namespace new noexcept not nullptr operator or private protected public register reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while xor";break;
    case CodeLanguage::Python:words="False None True and as assert async await break class continue def del elif else except finally for from global if import in is lambda nonlocal not or pass raise return try while with yield match case";break;
    case CodeLanguage::JavaScript:case CodeLanguage::TypeScript:words="abstract any as async await bigint boolean break case catch class const constructor continue debugger declare default delete do else enum export extends false finally for from function get if implements import in infer instanceof interface is keyof let module namespace never new null number object of package private protected public readonly require return set static string super switch symbol this throw true try type typeof undefined unique unknown var void while with yield";break;
    case CodeLanguage::Json:words="true false null";break;
    case CodeLanguage::Bash:words="if then else elif fi for while in do done case esac function select until time coproc echo printf export local readonly return exit set unset source test shift read exec trap";break;
    case CodeLanguage::PowerShell:words="begin break catch class continue data define do dynamicparam else elseif end enum exit filter finally for foreach from function if in param process return switch throw trap try until using var while workflow";lexer->WordListSet(1,"write-host write-output get-childitem get-content set-content invoke-webrequest foreach-object where-object select-object");break;
    case CodeLanguage::Sql:words="select from where join left right inner outer on as and or not null true false insert into values update set delete create table alter drop primary key foreign references index group by order having limit offset distinct union all case when then else end exists count sum avg min max asc desc with begin commit rollback";break;
    default:break;}
    lexer->WordListSet(0,words);if(language==CodeLanguage::Cpp)lexer->WordListSet(1,"size_t uint8_t uint16_t uint32_t uint64_t int32_t int64_t string wstring vector unique_ptr shared_ptr HWND HDC RECT POINT BOOL DWORD UINT");
    lexer->PropertySet("lexer.markdown.header.eolfill","1");lexer->PropertySet("lexer.cpp.track.preprocessor","0");lexer->PropertySet("lexer.cpp.backquoted.strings","1");lexer->PropertySet("lexer.json.allow.comments","1");lexer->Lex(0,document.Length(),0,&document);
    uint32_t wide=0;for(Sci_Position byte=0;byte<document.Length();){Sci_Position width{};const int cp=document.GetCharacterAndWidth(byte,&width);const uint32_t units=cp>0xffff?2u:1u;const auto color=Classify(language,static_cast<unsigned char>(document.StyleAt(byte)));
        if(!result.spans.empty()&&result.spans.back().color==color)result.spans.back().length+=units;else result.spans.push_back({wide,units,color});wide+=units;byte+=width;}
    if(input.size()>text.size())result.spans.push_back({static_cast<uint32_t>(text.size()),static_cast<uint32_t>(input.size()-text.size()),CodeColor::Text});
    return result;
}
HighlightWorker::HighlightWorker(HWND window):window_(window),thread_([this]{for(;;){Job job;{std::unique_lock lock(mutex_);condition_.wait(lock,[&]{return stopping_||pending_.has_value();});if(stopping_)return;job=std::move(*pending_);pending_.reset();}HighlightResult result;try{result=HighlightCode(job.text,job.language);}catch(...){result.language=CodeLanguage::Plain;}result.generation=job.generation;{std::lock_guard lock(mutex_);if(stopping_)return;if(job.generation!=latest_)continue;result_=std::move(result);}PostMessageW(window_,Ready,0,0);}}){}
HighlightWorker::~HighlightWorker(){{std::lock_guard lock(mutex_);stopping_=true;}condition_.notify_one();thread_.join();}
void HighlightWorker::Request(std::wstring text,CodeLanguage language,uint64_t generation){{std::lock_guard lock(mutex_);latest_=generation;pending_=Job{std::move(text),language,generation};result_.reset();}condition_.notify_one();}
std::optional<HighlightResult> HighlightWorker::Take(){std::lock_guard lock(mutex_);auto result=std::move(result_);result_.reset();return result;}
}
