// Synthetic hidden-window lifecycle test. Never reads/writes the OS clipboard.
#include "clipboard/preview_window.h"
#include "../src/clipboard/panel.cpp"
#include <psapi.h>
#include <iostream>
namespace lumashot {
struct ClipboardPanelTest {
static int Run(bool baseline){
 int failures=0;auto expect=[&](bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<std::endl;failures+=!ok;};
 auto sample=[](const char* stage,int cycle){PROCESS_MEMORY_COUNTERS_EX m{};m.cb=sizeof(m);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m));DWORD handles{};GetProcessHandleCount(GetCurrentProcess(),&handles);std::cout<<"MEMORY "<<stage<<" cycle="<<cycle<<" private="<<m.PrivateUsage<<" working="<<m.WorkingSetSize<<" handles="<<handles<<" gdi="<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<std::endl;};
 auto pump=[](){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}};
 const auto root=std::filesystem::temp_directory_path()/(L"LumaShot-disable-memory-test-"+std::to_wstring(GetCurrentProcessId()));
 struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code ec;std::filesystem::remove_all(p,ec);}}cleanup{root};
 ClipboardPanel panel(nullptr,[]{});auto& p=*panel.impl_;p.test_mode=true;sample("cold",0);
 for(int cycle=1;cycle<=30;++cycle){
  expect(p.Enable(true,cycle%2!=0),"enable or recreate hidden clipboard UI");p.disk=std::make_unique<clipboard::SessionStore>(root);expect(p.disk->WaitIdle(),"new session worker ready");
  for(int i=0;i<5;++i){clipboard::Entry e;e.text=i==2?L"synthetic.pdf":i==3?L"const synthetic = () => { return true; };":L"synthetic";e.kind=i<2?clipboard::Kind::Image:i==2?clipboard::Kind::Files:clipboard::Kind::Text;
   if(i<2){BITMAPINFOHEADER h{};h.biSize=sizeof(h);h.biWidth=512;h.biHeight=-512;h.biPlanes=1;h.biBitCount=32;std::vector<unsigned char> bytes(sizeof(h)+512*512*4);std::memcpy(bytes.data(),&h,sizeof(h));const uint32_t color=0xff4080a0u+static_cast<uint32_t>(i);for(size_t j=sizeof(h);j<bytes.size();j+=4)std::memcpy(bytes.data()+j,&color,4);e.formats.push_back({CF_DIB,std::move(bytes)});}
   else e.formats.push_back({CF_UNICODETEXT,{static_cast<unsigned char>(i),0,0,0}});
   expect(p.disk->Save(std::move(e)),"queue synthetic content without clipboard publication");
  }
  expect(p.disk->WaitIdle(),"save fixture");p.DrainStore();p.expanded=true;p.scale=2;p.Filter();for(const auto& e:p.history.entries)if(e.text.starts_with(L"const "))p.selected=e.id;
  SetWindowPos(p.window,nullptr,0,0,960,static_cast<int>(p.H*2),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);p.Render();p.disk->WaitIdle();p.DrainStore();p.file_icons->WaitIdle(10000);p.Render();pump();
  expect(p.surface&&p.target&&p.writer&&p.factory&&!p.fonts.empty(),"exercise full-size surface fonts factories without embedded content preview");
  if(cycle==1||cycle==10||cycle==30)sample("expanded",cycle);
  const auto session=p.disk->Directory();const auto original_window=p.window;const POINT anchor=p.anchor;
  p.Enable(false,false);pump();
  expect(!p.enabled&&p.history.entries.empty()&&!p.disk&&!std::filesystem::exists(session),"disable removes history and session files");
  if(!baseline){expect(!p.window&&!p.search&&!IsWindow(original_window),"disable destroys hidden native window and edit child");expect(!p.file_icons&&!p.surface&&!p.target&&!p.brush&&!p.factory&&!p.writer&&p.fonts.empty()&&!p.font&&!p.edit_brush,"disable releases clipboard workers rendering resources and fonts");expect(p.images.empty()&&p.file_bitmaps.empty()&&p.visible.empty()&&p.search_matches.empty()&&p.hits.empty()&&p.query.empty(),"disable releases cached selection search and hit-test state");expect(p.anchor.x==anchor.x&&p.anchor.y==anchor.y,"disable retains lightweight position for next enable");}
  if(cycle==1||cycle==10||cycle==20||cycle==30)sample("disabled",cycle);
 }
 expect(p.Enable(false,true),"repeated disable is idempotent");return failures?1:0;
}
};
}
int main(int argc,char**){CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);const int result=lumashot::ClipboardPanelTest::Run(argc>1);CoUninitialize();return result;}
