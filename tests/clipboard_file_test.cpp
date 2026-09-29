#include "export/clipboard.h"
#include "export/png.h"
#include <shellapi.h>
#include <objbase.h>
#include <algorithm>
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
 // A crop encoded through a view (no intermediate copy) equals the copied crop.
 auto big=MakeFrame({0,0,300,700});for(size_t i=0;i<big.pixels.size();++i)big.pixels[i]=0xff000000u|((static_cast<uint32_t>(i)*2654435761u)>>8);
 const RECT cut_rect{17,5,263,690};const Frame cut=Crop(big,cut_rect);
 for(int format=0;format<3;++format){
  const auto path=std::filesystem::temp_directory_path()/(std::wstring(L"LumaShot-view-test")+(format==1?L".jpg":format==2?L".bmp":L".png"));
  SaveImageFile(PixelsOf(big,cut_rect),path,format);const auto decoded=ReadPng(path);std::filesystem::remove(path);
  expect(decoded.Width()==246&&decoded.Height()==685,"view crop encodes at crop dimensions");
  if(format!=1)expect(decoded.pixels==cut.pixels,"lossless view crop equals the copied crop");
 }
 {auto staged_view=PrepareClipboardImage(PixelsOf(big,cut_rect));bool same=false;
  if(const auto* header=static_cast<const BITMAPINFOHEADER*>(GlobalLock(staged_view->image))){const auto* pixels=reinterpret_cast<const uint32_t*>(header+1);same=header->biWidth==246&&header->biHeight==685;
   for(int y=0;y<685&&same;++y)same=std::equal(pixels+static_cast<size_t>(684-y)*246,pixels+static_cast<size_t>(685-y)*246,cut.pixels.data()+static_cast<size_t>(y)*246);GlobalUnlock(staged_view->image);}
  expect(same,"clipboard DIB from a view crop equals the copied crop");}
 }catch(const std::exception& e){std::cout<<e.what()<<std::endl;++failures;}
 if(WaitClipboard(owner)){EmptyClipboard();CloseClipboard();}DestroyWindow(owner);CoUninitialize();return failures?1:0;
}



