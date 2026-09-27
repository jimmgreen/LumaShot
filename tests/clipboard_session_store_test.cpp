#include "clipboard/session_store.h"
#include <objbase.h>
#include <psapi.h>
#include <iostream>
#include <fstream>
#include <cstring>
using namespace lumashot::clipboard;
namespace {
size_t PrivateBytes(){PROCESS_MEMORY_COUNTERS_EX c{};c.cb=sizeof(c);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&c),sizeof(c));return c.PrivateUsage;}
Entry Synthetic(int i,size_t size=512*1024){Entry e;e.kind=Kind::Image;e.text=L"synthetic "+std::to_wstring(i);e.formats.push_back({CF_DIB,std::vector<unsigned char>(size,static_cast<unsigned char>(i))});return e;}
Entry Text(std::wstring text){Entry e;e.kind=Kind::Text;e.text=std::move(text);auto* p=reinterpret_cast<const unsigned char*>(e.text.c_str());e.formats.push_back({CF_UNICODETEXT,{p,p+(e.text.size()+1)*2}});return e;}
}
int wmain(int argc,wchar_t** argv){
 if(argc==3&&std::wstring(argv[1])==L"--abrupt-child"){CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);SessionStore store(argv[2]);store.Save(Synthetic(123));if(!store.WaitIdle()||store.Stats().disk_bytes==0)ExitProcess(2);ExitProcess(0);}
 CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int failures=0;auto expect=[&](bool value,const char* message){std::cout<<(value?"PASS ":"FAIL ")<<message<<std::endl;failures+=!value;};
 const auto root=std::filesystem::temp_directory_path()/(L"LumaShot-clipboard-session-test-"+std::to_wstring(GetCurrentProcessId()));std::filesystem::create_directories(root);
 struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code ec;std::filesystem::remove_all(p,ec);}}cleanup{root};
 std::filesystem::path directory;
 try{
  SessionStore store(root);expect(store.WaitIdle(),"worker initialization completes");directory=store.Directory();expect(!directory.empty()&&std::filesystem::exists(directory/L"lease"),"isolated session owns a lease");
  {SessionStore other(root);expect(other.WaitIdle()&&std::filesystem::exists(directory),"parallel instance never removes live session");}
  History history;
  auto accept=[&](){expect(store.WaitIdle(),"bounded worker finishes");auto results=store.Take();for(auto& result:results){expect(result.kind==StoreResultKind::Saved,"save completed without plaintext fallback");if(result.kind==StoreResultKind::Saved)expect(history.Add(std::move(result.entry)),"history accepts lightweight metadata");}};
  const auto baseline=PrivateBytes();
  for(int i=0;i<100;++i){expect(store.Save(Synthetic(i)),"queue synthetic image bytes");accept();if(i==9||i==49||i==99)std::cout<<"MEMORY items="<<i+1<<" private="<<PrivateBytes()<<" history_payload="<<history.Bytes()<<" disk="<<store.Stats().disk_bytes<<std::endl;}
  size_t resident=0;for(const auto& e:history.entries){resident+=e.text.capacity()*2;for(const auto& f:e.formats)resident+=f.bytes.capacity();expect(e.formats.empty()&&e.payload,"full payload not retained in history");}
  expect(history.entries.size()==100&&resident<256*1024,"100 payloads retain less than 256 KiB of index text");expect(store.Stats().pending_bytes==0&&store.Stats().result_bytes==0,"idle queues retain no plaintext payload");
  std::cout<<"MEMORY baseline="<<baseline<<" after100="<<PrivateBytes()<<" index_capacity="<<resident<<std::endl;
  const auto id=history.entries.front().id;expect(store.Load(history.entries.front(),77)&&store.WaitIdle(),"read only selected payload asynchronously");{auto results=store.Take();expect(results.size()==1&&results[0].kind==StoreResultKind::Loaded&&results[0].token==77&&results[0].entry.formats==Synthetic(99).formats,"selected payload round-trips byte exactly");}
  auto long_text=Text(std::wstring(5000,L'测')+L" unique-tail-needle");const auto expected=long_text.formats;expect(store.Save(std::move(long_text)),"save long text");accept();
  expect(history.entries.front().text.size()<=SessionStore::SummaryChars+1&&history.entries.front().formats.empty(),"long text summary bounded without truncating payload");
  store.Search(history.entries,L"UNIQUE-TAIL-NEEDLE",88);expect(store.WaitIdle(),"full text search finishes off UI thread");{auto results=store.Take();expect(results.size()==1&&results[0].kind==StoreResultKind::Search&&results[0].matches.size()==1&&results[0].matches[0]==history.entries.front().id,"search matches beyond the in-memory summary");}
  expect(store.Load(history.entries.front(),89)&&store.WaitIdle(),"load full text");{auto results=store.Take();expect(results.size()==1&&results[0].entry.formats==expected,"paste preserves full Unicode text and null terminator");}
  const auto payload=history.entries.front().payload;const auto encrypted=directory/(std::to_wstring(payload->key)+L".bin");{std::ifstream in(encrypted,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(in)),{});std::string needle(reinterpret_cast<const char*>(expected[0].bytes.data()),expected[0].bytes.size());expect(bytes.find(needle)==std::string::npos,"on-disk payload is not plaintext");}
  const auto text_id=history.entries.front().id;history.entries.front().favorite=true;expect(store.Save(Text(std::wstring(5000,L'测')+L" unique-tail-needle")),"queue duplicate");accept();expect(history.entries.front().id==text_id&&history.entries.front().favorite,"digest deduplication retains identity and favorite");
  expect(store.Save(Synthetic(900,History::MaxItemBytes+1))==false,"oversized entry rejected before queue growth");
  store.Load(history.entries.front(),90);store.CancelReads();expect(store.WaitIdle(),"cancel pending or active read");{const auto results=store.Take();expect(std::none_of(results.begin(),results.end(),[](const StoreResult& r){return r.token==90;}),"cancelled read cannot publish late payload");}
  const auto broken=history.entries.back();
  std::filesystem::copy_file(directory/(std::to_wstring(history.entries.front().payload->key)+L".bin"),directory/(std::to_wstring(broken.payload->key)+L".bin"),std::filesystem::copy_options::overwrite_existing);
  store.Load(broken,92);expect(store.WaitIdle(),"swapped valid ciphertext read returns");{const auto results=store.Take();expect(results.size()==1&&results[0].kind==StoreResultKind::Error,"digest rejects another valid encrypted item under selected key");}
  {std::ofstream out(directory/(std::to_wstring(broken.payload->key)+L".bin"),std::ios::binary|std::ios::trunc);out<<"corrupt";}
  store.Load(broken,91);expect(store.WaitIdle(),"corrupt payload read returns");{const auto results=store.Take();expect(results.size()==1&&results[0].kind==StoreResultKind::Error,"corruption fails closed without returning bytes");}
  const auto erased_key=history.Find(id)?history.Find(id)->payload->key:0;history.Erase(id);expect(store.WaitIdle(),"eviction cleanup completes");if(erased_key)expect(!std::filesystem::exists(directory/(std::to_wstring(erased_key)+L".bin")),"deleting entry removes corresponding disk payload");
  for(int cycle=0;cycle<100;++cycle){store.Load(history.entries.front(),1000+cycle);store.WaitIdle();{auto results=store.Take();expect(results.size()==1&&results[0].kind==StoreResultKind::Loaded,"repeat read owns only this request");}if(cycle==9||cycle==49||cycle==99)std::cout<<"REPEAT cycle="<<cycle+1<<" private="<<PrivateBytes()<<std::endl;}
  for(int i=0;i<10;++i)store.Save(Synthetic(1500+i));store.CancelSaves();expect(store.WaitIdle(),"cancel pending captures finishes");{auto results=store.Take();expect(results.empty(),"clear cannot resurrect late saved entries");}
  history.Clear(false);expect(store.WaitIdle()&&store.Stats().pending_bytes==0&&store.Stats().result_bytes==0,"clear releases index and queued payload buffers");
 }catch(const std::exception& e){std::cout<<"FAIL exception "<<e.what()<<std::endl;++failures;}
 expect(!directory.empty()&&!std::filesystem::exists(directory),"normal shutdown removes own session directory");
 const auto orphan=root/L"session-{12345678-1234-1234-1234-123456789012}";std::filesystem::create_directory(orphan);{std::ofstream out(orphan/L"lease");out<<"";std::ofstream body(orphan/L"1.bin");body<<"synthetic orphan";}
 {SessionStore next(root);expect(next.WaitIdle()&&!std::filesystem::exists(orphan),"next launch cleans abandoned session without restoring history");}
 wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);std::wstring command=L"\""+std::wstring(executable)+L"\" --abrupt-child \""+root.native()+L"\"";STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
 if(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)){
  const auto wait=WaitForSingleObject(process.hProcess,20000);if(wait!=WAIT_OBJECT_0)TerminateProcess(process.hProcess,3);DWORD code{};GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hThread);CloseHandle(process.hProcess);expect(wait==WAIT_OBJECT_0&&code==0,"independent child exits without store destructor");
  size_t abandoned=0;for(const auto& entry:std::filesystem::directory_iterator(root))if(entry.is_directory())++abandoned;expect(abandoned==1,"abrupt exit leaves exactly its encrypted session");
  {SessionStore next(root);expect(next.WaitIdle(),"restart cleanup finishes");size_t active=0;for(const auto& entry:std::filesystem::directory_iterator(root))if(entry.is_directory())++active;expect(active==1&&next.Stats().disk_bytes==0&&next.Take().empty(),"restart removes actual abandoned session and restores no history");}
 }else expect(false,"spawn isolated abnormal-exit fixture");
 CoUninitialize();return failures?1:0;
}
