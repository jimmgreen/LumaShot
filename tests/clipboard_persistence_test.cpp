#include "clipboard/session_store.h"
#include <objbase.h>
#include <algorithm>
#include <fstream>
#include <iostream>
using namespace lumashot::clipboard;
static Entry Sample(int n){Entry e;e.kind=n==1?Kind::Files:n==2?Kind::Image:Kind::Text;e.text=n==1?L"C:\\Synthetic\\one.txt\nC:\\Synthetic\\two.png":n==2?L"synthetic image":std::wstring(2000,L'测')+L" persistent tail";GetLocalTime(&e.time);e.formats.push_back({n==2?RegisterClipboardFormatW(L"LumaShot.Persistence.TestImage"):CF_UNICODETEXT,{static_cast<unsigned char>(n),5,8,13}});return e;}
int wmain(int argc,wchar_t** argv){
 CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 if(argc==3&&std::wstring(argv[1])==L"--crash"){SessionStore store(argv[2],nullptr,true);if(!store.WaitIdle())ExitProcess(2);store.Take();store.Save(Sample(0));if(!store.WaitIdle())ExitProcess(3);auto result=store.Take();if(result.size()!=1||result[0].kind!=StoreResultKind::Saved)ExitProcess(4);result[0].entry.favorite=true;store.Commit({result[0].entry});if(!store.WaitIdle())ExitProcess(5);ExitProcess(0);}
 int failures=0;auto expect=[&](bool good,const char* label){std::cout<<(good?"PASS ":"FAIL ")<<label<<std::endl;failures+=!good;};
 const auto root=std::filesystem::temp_directory_path()/(L"LumaShot-persistence-test-"+std::to_wstring(GetCurrentProcessId()));
 struct Cleanup{std::filesystem::path root;~Cleanup(){std::error_code ec;std::filesystem::remove_all(root,ec);}}cleanup{root};std::filesystem::path dir;SYSTEMTIME time{};
 {SessionStore store(root,nullptr,true);expect(store.WaitIdle(),"initialization finishes");auto initial=store.Take();expect(initial.size()==1&&initial[0].kind==StoreResultKind::Restored&&initial[0].entries.empty(),"new store starts empty");History history;
  for(int i=0;i<3;++i){expect(store.Save(Sample(i))&&store.WaitIdle(),"save encrypted fixture");for(auto& result:store.Take())expect(result.kind==StoreResultKind::Saved&&history.Add(std::move(result.entry)),"accept lightweight record");}
  history.entries.back().favorite=true;time=history.entries.back().time;{const auto work=history.AddGroup(L"工作");expect(work&&history.SetGroup(history.entries.front().id,work),"group synthetic record");}store.Commit(history.entries,history.groups);expect(store.WaitIdle(),"durable index commits");dir=store.Directory();
  std::ifstream in(dir/L"index.bin",std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(in)),{});expect(bytes.find("persistent tail")==std::string::npos&&bytes.size()>64,"index is encrypted");
  {SessionStore locked(root,nullptr,true);expect(locked.WaitIdle(),"second instance fails promptly");auto result=locked.Take();expect(result.size()==1&&result[0].kind==StoreResultKind::Error,"exclusive history lease prevents competing writers");}
 }
 expect(std::filesystem::exists(dir/L"index.bin"),"exit retains encrypted history outside installation directory");
 {SessionStore store(root,nullptr,true);expect(store.WaitIdle(),"reopen finishes");auto result=store.Take();expect(result.size()==1&&result[0].kind==StoreResultKind::Restored&&result[0].entries.size()==3,"restart restores all history");if(result.size()==1&&result[0].entries.size()==3){auto entries=result[0].entries;expect(entries.back().favorite&&entries.back().time.wMilliseconds==time.wMilliseconds,"pin and original timestamp survive restart");expect(entries[1].file_count==2&&entries[1].file_names[1]==L"two.png","multifile metadata restored");
   expect(result[0].groups.size()==1&&result[0].groups[0].name==L"工作"&&result[0].groups[0].color==History::Palette[0]&&entries.front().group==result[0].groups[0].id&&entries.back().group==0,"group table and membership survive restart (index v2)");
   for(size_t i=0;i<entries.size();++i){expect(entries[i].formats.empty()&&entries[i].payload,"only bounded summaries remain resident");expect(store.Load(entries[i],i+1)&&store.WaitIdle(),"read restored payload");auto loaded=store.Take();expect(loaded.size()==1&&loaded[0].kind==StoreResultKind::Loaded&&loaded[0].entry.formats==Sample(2-static_cast<int>(i)).formats,"payload including registered format round trips");}
   entries.erase(entries.begin());store.Commit(entries);expect(store.WaitIdle(),"delete commits");}}
 {SessionStore store(root,nullptr,true);expect(store.WaitIdle(),"reopen after deletion");auto result=store.Take();expect(result.size()==1&&result[0].entries.size()==2,"deleted item does not reappear");if(result.size()==1){auto entries=result[0].entries;std::erase_if(entries,[](const Entry& entry){return !entry.favorite;});store.Commit(entries);expect(store.WaitIdle(),"clear unpinned commits");}}
 {SessionStore store(root,nullptr,true);store.WaitIdle();auto result=store.Take();expect(result.size()==1&&result[0].entries.size()==1&&result[0].entries[0].favorite,"clear unpinned keeps persistent pin");store.Commit({});expect(store.WaitIdle(),"clear all commits");}
 {SessionStore store(root,nullptr,true);store.WaitIdle();auto result=store.Take();expect(result.size()==1&&result[0].entries.empty(),"clear all remains empty after restart");}
 wchar_t executable[MAX_PATH]{};GetModuleFileNameW(nullptr,executable,MAX_PATH);std::wstring command=L"\""+std::wstring(executable)+L"\" --crash \""+(root/L"crash").wstring()+L"\"";STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION child{};
 const bool started=CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)!=FALSE;expect(started,"launch isolated abrupt-exit fixture");if(started){WaitForSingleObject(child.hProcess,30000);DWORD code{};GetExitCodeProcess(child.hProcess,&code);expect(code==0,"child committed before abrupt exit");CloseHandle(child.hThread);CloseHandle(child.hProcess);SessionStore store(root/L"crash",nullptr,true);store.WaitIdle();auto result=store.Take();expect(result.size()==1&&result[0].entries.size()==1&&result[0].entries[0].favorite,"committed history survives abrupt process exit");}
 const auto damaged=root/L"damaged";std::filesystem::path damagedFile;
 {SessionStore store(damaged,nullptr,true);store.WaitIdle();store.Take();std::vector<Entry> entries;for(int i=0;i<2;++i){store.Save(Sample(i));store.WaitIdle();auto result=store.Take();if(result.size()==1)entries.push_back(std::move(result[0].entry));}store.Commit(entries);store.WaitIdle();if(!entries.empty())damagedFile=store.Directory()/(std::to_wstring(entries[0].payload->key)+L".bin");}
 if(!damagedFile.empty()){std::ofstream bad(damagedFile,std::ios::binary|std::ios::trunc);bad<<"damaged synthetic payload";bad.close();SessionStore store(damaged,nullptr,true);store.WaitIdle();auto result=store.Take();expect(result.size()==1&&result[0].kind==StoreResultKind::Restored&&result[0].entries.size()==1&&!result[0].message.empty(),"damaged payload does not hide remaining history");}
 {std::ofstream bad(damaged/L"history-v1"/L"index.bin",std::ios::binary|std::ios::trunc);bad<<"damaged index";}
 {SessionStore store(damaged,nullptr,true);store.WaitIdle();auto result=store.Take();expect(result.size()==1&&result[0].kind==StoreResultKind::Error,"damaged index reports failure without replacing existing files");store.Commit({});store.WaitIdle();store.Take();}
 expect(std::filesystem::file_size(damaged/L"history-v1"/L"index.bin")==13,"failed initialization cannot silently overwrite history index");
 // Session-only mode (the default) deletes history saved by an earlier opt-in.
 const auto optout=root/L"optout";
 {SessionStore store(optout,nullptr,true);store.WaitIdle();store.Take();store.Save(Sample(0));store.WaitIdle();auto saved=store.Take();if(saved.size()==1)store.Commit({saved[0].entry});expect(store.WaitIdle(),"opt-in history commits");
  expect(!SessionStore::PurgePersistent(optout),"purge refuses history held by a live store");expect(std::filesystem::exists(optout/L"history-v1"/L"index.bin"),"live history survives purge attempt");}
 {SessionStore session(optout,nullptr,false);expect(session.WaitIdle(),"session-only store starts");auto result=session.Take();expect(std::none_of(result.begin(),result.end(),[](const StoreResult& r){return r.kind==StoreResultKind::Error||!r.entries.empty();}),"session-only store restores nothing");
  expect(!std::filesystem::exists(optout/L"history-v1"),"session-only store deletes previously persisted history");}
 {SessionStore store(optout,nullptr,true);store.WaitIdle();auto result=store.Take();expect(result.size()==1&&result[0].entries.empty(),"re-enabling persistence starts empty");}
 expect(SessionStore::PurgePersistent(optout)&&!std::filesystem::exists(optout/L"history-v1"),"explicit purge removes idle history");
 expect(SessionStore::PurgePersistent(root/L"missing"),"purge of absent history succeeds");
 CoUninitialize();return failures?1:0;
}