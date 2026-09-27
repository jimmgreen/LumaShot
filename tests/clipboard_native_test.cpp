#include "clipboard/native.h"
#include "export/png.h"
#include <shellapi.h>
#include <shlobj.h>
#include <fstream>
#include <iostream>
#include <cstring>
using namespace lumashot;
using namespace lumashot::clipboard;
// A private window station has its own clipboard. No access to the user's clipboard.
struct IsolatedDesktop {
    HWINSTA original{GetProcessWindowStation()},station{};
    HDESK original_desktop{GetThreadDesktop(GetCurrentThreadId())},desktop{};
    bool Init(){
        station=CreateWindowStationW(nullptr,0,WINSTA_ALL_ACCESS,nullptr);
        if(!station||!SetProcessWindowStation(station))return false;
        desktop=CreateDesktopW(L"LumaShotSynthetic",nullptr,nullptr,0,GENERIC_ALL,nullptr);
        return desktop&&SetThreadDesktop(desktop);
    }
    ~IsolatedDesktop(){SetThreadDesktop(original_desktop);SetProcessWindowStation(original);if(desktop)CloseDesktop(desktop);if(station)CloseWindowStation(station);}
};
int main(){
    IsolatedDesktop isolation;if(!isolation.Init()){std::cerr<<"FAIL cannot create isolated clipboard desktop: "<<GetLastError()<<std::endl;return 1;}
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int failures=0;auto expect=[&](bool ok,const char* message){std::cout<<(ok?"PASS ":"FAIL ")<<message<<std::endl;failures+=!ok;};
    HWND owner=CreateWindowExW(0,L"STATIC",L"Synthetic owner",WS_POPUP,0,0,400,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    HWND edit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|ES_MULTILINE,0,0,300,100,owner,nullptr,GetModuleHandleW(nullptr),nullptr);
    expect(owner&&edit,"native controls exist on isolated desktop");
    const auto root=std::filesystem::temp_directory_path()/(L"LumaShot-native-test-"+std::to_wstring(GetCurrentProcessId()));std::filesystem::create_directories(root);
    auto text=[](const std::wstring& value){Entry e;e.kind=Kind::Text;e.text=value;const auto* p=reinterpret_cast<const unsigned char*>(value.c_str());e.formats.push_back({CF_UNICODETEXT,{p,p+(value.size()+1)*2}});return e;};
    try{
        auto original=text(L"Synthetic 中文\r\nUnicode \U0001f4cb");expect(Write(owner,original),"publish Unicode fixture through production writer");
        bool busy{};auto read=Read(owner,busy);expect(read&&!busy&&read->kind==Kind::Text&&read->text==original.text,"production listener reads full Unicode text");
        SendMessageW(edit,WM_PASTE,0,0);wchar_t value[128]{};GetWindowTextW(edit,value,128);expect(value==original.text,"native Edit pastes exact multiline Unicode text");
        const auto before=GetClipboardSequenceNumber();expect(!Write(owner,Entry{})&&before==GetClipboardSequenceNumber(),"empty publication cannot erase existing clipboard");
        const UINT html=RegisterClipboardFormatW(L"HTML Format"),rtf=RegisterClipboardFormatW(L"Rich Text Format");
        auto rich=text(std::wstring(12000,L'文')+L"\r\nFull Unicode \U0001f4cb");
        const std::string html_text="Version:0.9\r\nStartHTML:-1\r\nEndHTML:-1\r\nStartFragment:-1\r\nEndFragment:-1\r\n<html><body><b>Synthetic rich fixture</b></body></html>";
        const std::string rtf_text="{\\rtf1\\ansi Synthetic \\b rich\\b0 fixture}";
        rich.formats.push_back({html,{html_text.begin(),html_text.end()}});rich.formats.back().bytes.push_back(0);
        rich.formats.push_back({rtf,{rtf_text.begin(),rtf_text.end()}});rich.formats.back().bytes.push_back(0);
        expect(Write(owner,rich),"publish synthetic HTML and RTF with full Unicode");
        read=Read(owner,busy);expect(read&&read->formats==rich.formats,"capture preserves both rich formats byte for byte");
        expect(read&&Write(owner,*read),"ordinary restore publishes captured rich text");
        auto restored=Read(owner,busy);expect(restored&&restored->formats==rich.formats,"ordinary restore retains rich payloads");
        rich.text=L"Short display preview";
        expect(Write(owner,rich,true),"explicit plain text publishes original Unicode payload");
        read=Read(owner,busy);expect(read&&read->text.size()>12000&&read->formats.size()==1&&read->formats.front()==rich.formats.front(),"plain text uses full stored payload instead of preview");
        expect(!IsClipboardFormatAvailable(html)&&!IsClipboardFormatAvailable(rtf),"plain text omits HTML and RTF");
        for(const auto kind:{Kind::Image,Kind::Files}){auto invalid=rich;invalid.kind=kind;const auto sequence=GetClipboardSequenceNumber();expect(!Write(owner,invalid,true)&&sequence==GetClipboardSequenceNumber(),"plain text rejects nontext without changing clipboard");}
        Entry missing;missing.kind=Kind::Text;missing.formats.push_back(rich.formats[1]);const auto sequence=GetClipboardSequenceNumber();
        expect(!Write(owner,missing,true)&&sequence==GetClipboardSequenceNumber(),"missing Unicode payload cannot erase clipboard");
        auto oversized=text(L"Synthetic budget fixture");oversized.formats.push_back({html,std::vector<unsigned char>(History::MaxItemBytes/2,1)});oversized.formats.push_back({rtf,std::vector<unsigned char>(History::MaxItemBytes/2,2)});
        expect(Write(owner,oversized)&&!Read(owner,busy)&&!busy,"aggregate rich text item limit rejects oversized capture");
        auto private_text=text(L"Synthetic excluded fixture");DWORD no=0;const auto* bytes=reinterpret_cast<const unsigned char*>(&no);private_text.formats.push_back({RegisterClipboardFormatW(L"CanIncludeInClipboardHistory"),{bytes,bytes+4}});
        expect(Write(owner,private_text)&&!Read(owner,busy)&&!busy,"privacy marker excludes synthetic content");
        Entry files;files.kind=Kind::Files;std::wstring paths;
        for(const auto* name:{L"synthetic-a.txt",L"synthetic-b.txt"}){const auto path=root/name;{std::ofstream file(path);file<<"synthetic";}paths+=path.wstring();paths+=L'\0';}paths+=L'\0';
        DROPFILES drop{};drop.pFiles=sizeof(drop);drop.fWide=TRUE;std::vector<unsigned char> data(sizeof(drop)+paths.size()*2);std::memcpy(data.data(),&drop,sizeof(drop));std::memcpy(data.data()+sizeof(drop),paths.data(),paths.size()*2);files.formats.push_back({CF_HDROP,data});
        expect(Write(owner,files),"publish synthetic file list");read=Read(owner,busy);expect(read&&read->kind==Kind::Files&&read->file_count==2&&!read->file_names[1].empty()&&read->text.find(L"synthetic-a.txt")!=std::wstring::npos&&read->text.find(L"synthetic-b.txt")!=std::wstring::npos,"read every file path");
        expect(read&&Write(owner,*read),"restore file list through production writer");if(OpenClipboard(owner)){expect(DragQueryFileW(static_cast<HDROP>(GetClipboardData(CF_HDROP)),0xffffffff,nullptr,0)==2,"native shell format retains both files");CloseClipboard();}else expect(false,"open isolated file clipboard");
        for(UINT format:{UINT(CF_DIB),UINT(CF_DIBV5)}){
            BITMAPV5HEADER h{};h.bV5Size=format==CF_DIB?sizeof(BITMAPINFOHEADER):sizeof(h);h.bV5Width=32;h.bV5Height=-24;h.bV5Planes=1;h.bV5BitCount=32;
            std::vector<unsigned char> dib(h.bV5Size+32*24*4);std::memcpy(dib.data(),&h,h.bV5Size);const uint32_t color=0xff4080a0;for(size_t i=h.bV5Size;i<dib.size();i+=4)std::memcpy(dib.data()+i,&color,4);
            Entry image;image.kind=Kind::Image;image.formats.push_back({format,dib});expect(Write(owner,image),"publish bitmap fixture");read=Read(owner,busy);expect(read&&read->kind==Kind::Image&&Thumbnail(*read),"bitmap capture and decode work through native formats");
        }
        const auto path=root/L"synthetic-4k.png";SavePng(MakeFrame({0,0,3840,2160},0xff4080a0),path);std::ifstream file(path,std::ios::binary);std::vector<unsigned char> png((std::istreambuf_iterator<char>(file)),{});
        Entry image;image.kind=Kind::Image;image.formats.push_back({RegisterClipboardFormatW(L"PNG"),png});expect(Write(owner,image),"publish PNG fixture");read=Read(owner,busy);expect(read&&read->kind==Kind::Image&&Thumbnail(*read,128,32ull*1024*1024),"4K PNG native capture and thumbnail decode work");
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;++failures;}
    if(OpenClipboard(owner)){EmptyClipboard();CloseClipboard();}DestroyWindow(owner);std::error_code ec;std::filesystem::remove_all(root,ec);CoUninitialize();return failures?1:0;
}
