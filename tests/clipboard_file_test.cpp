#include "export/clipboard.h"
#include "export/png.h"
#include <shellapi.h>
#include <objbase.h>
#include <iostream>
#include <functional>
// Clipboard listeners can briefly hold the shared system clipboard between assertions.
bool WaitClipboard(HWND owner){for(int i=0;i<40;++i){if(OpenClipboard(owner))return true;Sleep(5);}return false;}
void RetryClipboard(const std::function<void()>& action){for(int i=0;;++i){try{action();return;}catch(const std::exception& e){if(i>=39||std::string(e.what()).find("Clipboard is busy")==std::string::npos)throw;Sleep(5);}}}
int main(){
 using namespace lumashot;CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int failures=0;
 auto expect=[&](bool ok,const char* message){std::cout<<(ok?"PASS ":"FAIL ")<<message<<std::endl;failures+=!ok;};
 HWND owner=CreateWindowExW(0,L"STATIC",L"Synthetic clipboard test",WS_POPUP,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 try{
 auto frame=MakeFrame({0,0,37,29},0xff2486cb);
 for(int format=0;format<3;++format){
 auto file=PrepareClipboardFile(frame,format);const auto path=file->path;auto decoded=ReadPng(path);
 expect(decoded.Width()==37&&decoded.Height()==29,"all file formats decode at original dimensions");
 if(format!=1)expect(decoded.pixels==frame.pixels,"lossless format retains pixels");
 RetryClipboard([&]{CopyImage(owner,frame,file);});expect(file->published,"successful clipboard publication retains file");
 bool valid=false;if(WaitClipboard(owner)){auto drop=static_cast<HDROP>(GetClipboardData(CF_HDROP));wchar_t name[32768]{};valid=drop&&DragQueryFileW(drop,0,name,32768)&&path==name&&IsClipboardFormatAvailable(CF_DIB);CloseClipboard();}
 expect(valid,"clipboard offers image and exact file path simultaneously");
 RetryClipboard([&]{CopyImage(owner,frame);});expect(!IsClipboardFormatAvailable(CF_HDROP)&&IsClipboardFormatAvailable(CF_DIB),"disabled file option publishes image only");
 file.reset();expect(std::filesystem::exists(path),"published file survives result destruction");std::filesystem::remove(path);
 }
 std::filesystem::path abandoned;{auto file=PrepareClipboardFile(frame,0);abandoned=file->path;}expect(!std::filesystem::exists(abandoned),"cancelled export deletes unpublished file");
 auto source=MakeFrame({0,0,2,2});source.pixels={0xff112233,0xff445566,0xff778899,0xffaabbcc};
 RetryClipboard([&]{CopyText(owner,L"synthetic staging sentinel");});const auto sequence=GetClipboardSequenceNumber();
 auto staged=PrepareClipboardImage(source);source.pixels.clear();expect(GetClipboardSequenceNumber()==sequence,"background preparation leaves existing clipboard untouched");
 RetryClipboard([&]{PublishClipboardImage(owner,*staged);});expect(!staged->image,"publication transfers ownership without recopying pixels");
 bool orientation=false;if(WaitClipboard(owner)){auto handle=GetClipboardData(CF_DIB);const auto* header=static_cast<const BITMAPINFOHEADER*>(GlobalLock(handle));if(header){const auto* pixels=reinterpret_cast<const uint32_t*>(header+1);orientation=header->biWidth==2&&header->biHeight==2&&pixels[0]==0xff778899&&pixels[3]==0xff445566;GlobalUnlock(handle);}CloseClipboard();}expect(orientation,"prepared clipboard retains bottom-up orientation after source release");
 bool invalid=false;try{PrepareClipboardImage(source);}catch(const std::invalid_argument&){invalid=true;}expect(invalid,"malformed image rejected before clipboard mutation"); bool rejected=false;try{PrepareClipboardFile(frame,99);}catch(const std::invalid_argument&){rejected=true;}expect(rejected,"invalid format rejected");
 }catch(const std::exception& e){std::cout<<e.what()<<std::endl;++failures;}
 if(WaitClipboard(owner)){EmptyClipboard();CloseClipboard();}DestroyWindow(owner);CoUninitialize();return failures?1:0;
}



