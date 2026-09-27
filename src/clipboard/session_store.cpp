#include "clipboard/session_store.h"
#include "clipboard/native.h"
#include <wincrypt.h>
#include <bcrypt.h>
#include <objbase.h>
#include <fstream>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <map>
#include <cstring>
#include <stdexcept>
namespace lumashot::clipboard {
namespace {
void Require(bool value){if(!value)throw std::runtime_error("clipboard session storage");}
struct Handle{HANDLE h{INVALID_HANDLE_VALUE};~Handle(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
struct Secret{DATA_BLOB data{};~Secret(){if(data.pbData){SecureZeroMemory(data.pbData,data.cbData);LocalFree(data.pbData);}}};
struct Bytes{std::vector<unsigned char> data;~Bytes(){if(!data.empty())SecureZeroMemory(data.data(),data.size());}};
void Number(std::vector<unsigned char>& b,uint32_t n){for(int i=0;i<4;++i)b.push_back(static_cast<unsigned char>(n>>(8*i)));}
uint32_t Number(const unsigned char*& p,const unsigned char* end){Require(end-p>=4);uint32_t n=0;for(int i=0;i<4;++i)n|=uint32_t(*p++)<<(8*i);return n;}
std::vector<unsigned char> Encode(const Entry& e){
    Require(!e.formats.empty()&&e.formats.size()<=16&&e.Bytes()<=History::MaxItemBytes);
    std::vector<unsigned char> b;b.reserve(e.Bytes()+128);Number(b,0x3253434c);Number(b,static_cast<uint32_t>(e.kind));Number(b,static_cast<uint32_t>(e.text.size()));
    const auto* t=reinterpret_cast<const unsigned char*>(e.text.data());b.insert(b.end(),t,t+e.text.size()*2);Number(b,static_cast<uint32_t>(e.formats.size()));
    for(const auto& f:e.formats){Number(b,f.id);wchar_t name[256]{};const int length=f.id>=0xc000?GetClipboardFormatNameW(f.id,name,256):0;Number(b,static_cast<uint32_t>(length));for(int i=0;i<length;++i)Number(b,name[i]);Number(b,static_cast<uint32_t>(f.bytes.size()));b.insert(b.end(),f.bytes.begin(),f.bytes.end());}return b;
}
Entry Decode(const DATA_BLOB& b){
    Require(b.pbData&&b.cbData>=16);const auto* p=b.pbData;const auto* end=p+b.cbData;const auto version=Number(p,end);Require(version==0x3153434c||version==0x3253434c);const auto kind=Number(p,end);Require(kind<=2);Entry e;e.kind=static_cast<Kind>(kind);
    const auto chars=Number(p,end);Require(chars<=History::MaxItemBytes/2&&size_t(end-p)>=size_t(chars)*2);e.text.resize(chars);std::memcpy(e.text.data(),p,size_t(chars)*2);p+=size_t(chars)*2;
    const auto count=Number(p,end);Require(count>0&&count<=16);for(uint32_t i=0;i<count;++i){auto id=Number(p,end);if(version==0x3253434c){const auto length=Number(p,end);Require(length<256);std::wstring name;for(uint32_t j=0;j<length;++j)name+=static_cast<wchar_t>(Number(p,end));if(length){id=RegisterClipboardFormatW(name.c_str());Require(id!=0);}}const auto size=Number(p,end);Require(size<=History::MaxItemBytes&&size_t(end-p)>=size);e.formats.push_back({id,{p,p+size}});p+=size;}
    Require(p==end&&e.Bytes()<=History::MaxItemBytes);return e;
}
std::wstring Lower(std::wstring s){for(auto& c:s)c=static_cast<wchar_t>(std::towlower(c));return s;}
bool PlainDirectory(const std::filesystem::path& p){const auto a=GetFileAttributesW(p.c_str());return a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_DIRECTORY)&&!(a&FILE_ATTRIBUTE_REPARSE_POINT);}
// Remove only flat files of an unleased history directory; never follow reparse points.
bool PurgeHistoryDirectory(const std::filesystem::path& dir){
    std::error_code ec;if(!std::filesystem::exists(dir,ec))return true;if(!PlainDirectory(dir))return false;
    Handle check;check.h=CreateFileW((dir/L"lease").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_HIDDEN,nullptr);
    if(check.h==INVALID_HANDLE_VALUE&&GetLastError()!=ERROR_FILE_NOT_FOUND)return false;
    if(check.h!=INVALID_HANDLE_VALUE){CloseHandle(check.h);check.h=INVALID_HANDLE_VALUE;}
    for(const auto& f:std::filesystem::directory_iterator(dir,ec)){const auto a=GetFileAttributesW(f.path().c_str());if(a==INVALID_FILE_ATTRIBUTES||(a&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return false;}
    if(ec)return false;
    for(const auto& f:std::filesystem::directory_iterator(dir,ec)){std::error_code ignored;std::filesystem::remove(f.path(),ignored);}
    std::filesystem::remove(dir,ec);return !ec;
}
}
struct SessionStore::State {
    struct SearchTask{std::vector<Entry> entries;std::vector<Group> groups;std::wstring query;size_t cursor{};bool incomplete{};std::vector<uint64_t> matches;uint64_t epoch{};};
    enum class KindJob {Save,Load,Thumb,Preview,Search,Commit};
    struct Job{KindJob kind;Entry entry;uint64_t token{},generation{};std::shared_ptr<SearchTask> search;uint64_t preview_epoch{};};
    std::filesystem::path root,directory;HWND notify{};Handle lease;bool persistent{};std::vector<Entry> committed;
    std::mutex mutex,garbage_mutex;std::condition_variable cv;std::deque<Job> jobs;std::vector<StoreResult> results;std::vector<uint64_t> garbage;
    std::atomic<size_t> garbage_count{},disk_bytes{};std::atomic<bool> stop{};
    std::map<uint64_t,size_t> files;std::thread worker;bool busy{},initialized{},startup_done{};size_t pending{},result_bytes{};uint64_t generation{},save_generation{},search_epoch{},preview_epoch{},serial{};
    explicit State(std::filesystem::path base,HWND w,bool keep):root(std::move(base)),notify(w),persistent(keep){}
    void Release(uint64_t key){if(stop)return;{std::lock_guard lock(garbage_mutex);garbage.push_back(key);++garbage_count;}cv.notify_all();}
    void Cleanup(){std::vector<uint64_t> keys;{std::lock_guard lock(garbage_mutex);keys.swap(garbage);}
        for(auto key:keys){auto it=files.find(key);if(it==files.end())continue;std::error_code ec;if(std::filesystem::remove(directory/(std::to_wstring(key)+L".bin"),ec)||!ec){disk_bytes-=it->second;files.erase(it);}}
        garbage_count-=keys.size();cv.notify_all();
    }
    void Init(){
        std::filesystem::create_directories(root);Require(PlainDirectory(root));
        if(persistent){
            const auto dir=root/L"history-v1";std::filesystem::create_directories(dir);Require(PlainDirectory(dir));
            lease.h=CreateFileW((dir/L"lease").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_HIDDEN,nullptr);Require(lease.h!=INVALID_HANDLE_VALUE);
            {std::lock_guard lock(mutex);directory=dir;initialized=true;}return;
        }
        // Serialize orphan cleanup and directory/lease creation between app instances.
        Handle gate;for(int i=0;i<100&&!stop;++i){gate.h=CreateFileW((root/L"cleanup.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_HIDDEN,nullptr);if(gate.h!=INVALID_HANDLE_VALUE)break;Sleep(10);}Require(gate.h!=INVALID_HANDLE_VALUE);
        for(const auto& item:std::filesystem::directory_iterator(root)){
            const auto name=item.path().filename().wstring();if(name.size()!=46||!name.starts_with(L"session-{")||name.back()!=L'}'||!PlainDirectory(item.path()))continue;
            Handle check;check.h=CreateFileW((item.path()/L"lease").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_HIDDEN,nullptr);
            if(check.h==INVALID_HANDLE_VALUE)continue;CloseHandle(check.h);check.h=INVALID_HANDLE_VALUE;
            // Never traverse reparse points inside an orphaned session.
            bool safe=true;for(const auto& f:std::filesystem::directory_iterator(item.path())){const auto a=GetFileAttributesW(f.path().c_str());if(a==INVALID_FILE_ATTRIBUTES||(a&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))){safe=false;break;}}
            if(safe){std::error_code ec;std::filesystem::remove_all(item.path(),ec);}
        }
        // Session-only mode: history saved by an earlier opt-in must not survive.
        PurgeHistoryDirectory(root/L"history-v1");
        GUID guid{};Require(SUCCEEDED(CoCreateGuid(&guid)));wchar_t name[40]{};StringFromGUID2(guid,name,40);const auto dir=root/(L"session-"+std::wstring(name));std::filesystem::create_directory(dir);
        lease.h=CreateFileW((dir/L"lease").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_HIDDEN,nullptr);Require(lease.h!=INVALID_HANDLE_VALUE);
        {std::lock_guard lock(mutex);directory=dir;initialized=true;}
    }
    Entry Read(const Entry& meta){
        Require(meta.payload!=nullptr);const auto path=directory/(std::to_wstring(meta.payload->key)+L".bin");const auto size=std::filesystem::file_size(path);Require(size>0&&size<=History::MaxItemBytes+65536);
        Bytes encrypted;encrypted.data.resize(static_cast<size_t>(size));std::ifstream in(path,std::ios::binary);Require(bool(in.read(reinterpret_cast<char*>(encrypted.data.data()),static_cast<std::streamsize>(size))));
        DATA_BLOB input{static_cast<DWORD>(size),encrypted.data.data()};Secret plain;Require(CryptUnprotectData(&input,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&plain.data)!=FALSE);
        BYTE digest[32]{};DWORD digest_size=32;Require(CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM,0,nullptr,plain.data.pbData,plain.data.cbData,digest,&digest_size)!=FALSE);
        Require(meta.payload->digest==std::string(reinterpret_cast<char*>(digest),32));
        Entry result=Decode(plain.data);result.id=meta.id;result.time=meta.time;result.favorite=meta.favorite;result.group=meta.group;result.file_count=meta.file_count;result.file_names=meta.file_names;return result;
    }
    static void Summarize(Entry& entry){
        if(entry.kind==Kind::Files){size_t start=0;uint32_t count=0;while(start<entry.text.size()){
            const auto end=entry.text.find(L'\n',start);const auto path=entry.text.substr(start,end==std::wstring::npos?end:end-start);
            if(count<entry.file_names.size()){const auto slash=path.find_last_of(L"\\/");entry.file_names[count]=path.substr(slash==std::wstring::npos?0:slash+1,260);}
            ++count;if(end==std::wstring::npos)break;start=end+1;}entry.file_count=count;}
        std::vector<Format>().swap(entry.formats);
        if(entry.text.size()>SessionStore::SummaryChars){std::wstring summary=entry.text.substr(0,SessionStore::SummaryChars);if(!summary.empty()&&summary.back()>=0xd800&&summary.back()<=0xdbff)summary.pop_back();summary+=L"…";entry.text=std::move(summary);}
    }
    // Index v2 ("LCH2"): v1 entry records plus a group id, followed by the group table.
    void CommitIndex(const std::vector<Entry>& entries,const std::vector<Group>& groups){
        if(!persistent)return;Require(entries.size()<=History::MaxTotal&&groups.size()<=History::MaxGroups);
        Bytes plain;Number(plain.data,0x3248434c);Number(plain.data,static_cast<uint32_t>(entries.size()));
        for(const auto& entry:entries){Require(entry.payload&&entry.payload->digest.size()==32);const auto& payload=*entry.payload;
            Number(plain.data,static_cast<uint32_t>(payload.key));Number(plain.data,static_cast<uint32_t>(payload.key>>32));Number(plain.data,static_cast<uint32_t>(payload.bytes));
            plain.data.insert(plain.data.end(),payload.digest.begin(),payload.digest.end());
            const auto* time=reinterpret_cast<const unsigned char*>(&entry.time);plain.data.insert(plain.data.end(),time,time+sizeof(SYSTEMTIME));Number(plain.data,entry.favorite?1:0);Number(plain.data,entry.group);
        }
        Number(plain.data,static_cast<uint32_t>(groups.size()));
        for(const auto& group:groups){Require(group.name.size()<=History::MaxGroupName);Number(plain.data,group.id);Number(plain.data,group.color);Number(plain.data,static_cast<uint32_t>(group.name.size()));
            const auto* name=reinterpret_cast<const unsigned char*>(group.name.data());plain.data.insert(plain.data.end(),name,name+group.name.size()*sizeof(wchar_t));}
        DATA_BLOB input{static_cast<DWORD>(plain.data.size()),plain.data.data()};Secret encrypted;Require(CryptProtectData(&input,L"LumaShot clipboard history",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&encrypted.data)!=FALSE);
        const auto temp=directory/L"index.tmp",path=directory/L"index.bin";
        Handle file;file.h=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);Require(file.h!=INVALID_HANDLE_VALUE);DWORD written{};
        Require(WriteFile(file.h,encrypted.data.pbData,encrypted.data.cbData,&written,nullptr)&&written==encrypted.data.cbData&&FlushFileBuffers(file.h));CloseHandle(file.h);file.h=INVALID_HANDLE_VALUE;
        Require(MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE);committed=entries;
    }
    void Restore(const std::shared_ptr<State>& self){
        if(!persistent)return;
        for(const auto& item:std::filesystem::directory_iterator(directory)){const auto name=item.path().stem().wstring();if(item.path().extension()!=L".bin"||name.empty()||name.find_first_not_of(L"0123456789")!=std::wstring::npos)continue;
            const auto attrs=GetFileAttributesW(item.path().c_str());Require(!(attrs&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)));const auto key=std::stoull(name);const auto size=std::filesystem::file_size(item.path());files[key]=static_cast<size_t>(size);disk_bytes+=static_cast<size_t>(size);serial=std::max(serial,key);}
        StoreResult result;result.kind=StoreResultKind::Restored;const auto path=directory/L"index.bin";
        if(std::filesystem::exists(path)){
            const auto size=std::filesystem::file_size(path);Require(size>0&&size<65536);Bytes encrypted;encrypted.data.resize(static_cast<size_t>(size));std::ifstream in(path,std::ios::binary);Require(bool(in.read(reinterpret_cast<char*>(encrypted.data.data()),static_cast<std::streamsize>(size))));
            DATA_BLOB input{static_cast<DWORD>(size),encrypted.data.data()};Secret plain;Require(CryptUnprotectData(&input,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&plain.data));
            const unsigned char* cursor=plain.data.pbData;const auto end=cursor+plain.data.cbData;const auto magic=Number(cursor,end);Require(magic==0x3148434c||magic==0x3248434c);const bool v2=magic==0x3248434c;const auto count=Number(cursor,end);Require(count<=History::MaxTotal);size_t total=0;
            for(uint32_t i=0;i<count;++i){Entry meta;auto payload=std::make_shared<DiskPayload>();const uint64_t low=Number(cursor,end),high=Number(cursor,end);payload->key=low|(high<<32);payload->bytes=Number(cursor,end);Require(payload->bytes<=History::MaxItemBytes&&end-cursor>=32+sizeof(SYSTEMTIME));payload->digest.assign(reinterpret_cast<const char*>(cursor),32);cursor+=32;std::memcpy(&meta.time,cursor,sizeof(SYSTEMTIME));cursor+=sizeof(SYSTEMTIME);meta.favorite=Number(cursor,end)!=0;if(v2)meta.group=Number(cursor,end);meta.payload=payload;
                try{auto entry=Read(meta);Require(entry.Bytes()==payload->bytes&&total+payload->bytes<=History::MaxBytes);entry.payload=payload;Summarize(entry);std::weak_ptr<State> weak=self;payload->release=[weak](uint64_t key){if(auto state=weak.lock())state->Release(key);};total+=payload->bytes;result.entries.push_back(std::move(entry));}catch(...){result.message=L"部分历史损坏，已恢复其余记录";}
            }
            if(v2){const auto group_count=Number(cursor,end);Require(group_count<=History::MaxGroups);
                for(uint32_t i=0;i<group_count;++i){Group group;group.id=Number(cursor,end);group.color=Number(cursor,end);const auto length=Number(cursor,end);Require(length<=History::MaxGroupName&&static_cast<size_t>(end-cursor)>=length*sizeof(wchar_t));
                    group.name.resize(length);std::memcpy(group.name.data(),cursor,length*sizeof(wchar_t));cursor+=length*sizeof(wchar_t);result.groups.push_back(std::move(group));}}
            Require(cursor==end);
        }
        committed=result.entries;
        // Only remove unreferenced payloads after a valid index has been read.
        for(const auto& [key,size]:files){(void)size;if(std::none_of(committed.begin(),committed.end(),[&](const Entry& entry){return entry.payload->key==key;}))Release(key);}
        std::lock_guard lock(mutex);results.push_back(std::move(result));if(notify)PostMessageW(notify,SessionStoreReady,0,0);
    }
    Entry Write(Entry entry,const std::shared_ptr<State>& self){
        Bytes plain;plain.data=Encode(entry);BYTE digest[32]{};DWORD digest_size=32;Require(CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM,0,nullptr,plain.data.data(),static_cast<DWORD>(plain.data.size()),digest,&digest_size)!=FALSE);
        DATA_BLOB input{static_cast<DWORD>(plain.data.size()),plain.data.data()};Secret encrypted;Require(CryptProtectData(&input,L"LumaShot temporary clipboard",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&encrypted.data)!=FALSE);
        Require(disk_bytes+encrypted.data.cbData<=SessionStore::DiskLimit);const uint64_t key=++serial;const auto path=directory/(std::to_wstring(key)+L".bin"),temp=directory/(std::to_wstring(key)+L".tmp");
        {std::ofstream out(temp,std::ios::binary|std::ios::trunc);Require(bool(out.write(reinterpret_cast<const char*>(encrypted.data.pbData),encrypted.data.cbData)));out.flush();Require(bool(out));}
        if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH)){std::error_code ec;std::filesystem::remove(temp,ec);throw std::runtime_error("commit clipboard");}
        auto payload=std::make_shared<DiskPayload>();payload->key=key;payload->bytes=entry.Bytes();payload->digest.assign(reinterpret_cast<char*>(digest),32);std::weak_ptr<State> weak=self;payload->release=[weak](uint64_t id){if(auto s=weak.lock())s->Release(id);};
        files.emplace(key,encrypted.data.cbData);disk_bytes+=encrypted.data.cbData;entry.payload=std::move(payload);std::vector<Format>().swap(entry.formats);
        Summarize(entry);return entry;
    }
    static size_t ResultBytes(const StoreResult& r){
        return (r.entry.formats.empty()?0:r.entry.Bytes()) + (r.image?r.image->pixels.size()*4:0)
            + (r.preview.image?r.preview.image->pixels.size()*4:0) + r.preview.text.size()*sizeof(wchar_t);
    }
    void DropPreviewLocked(){
        ++preview_epoch;
        std::erase_if(jobs,[](const Job& job){return job.kind==KindJob::Preview;});
        std::erase_if(results,[](const StoreResult& result){return result.preview_read;});
        result_bytes=0;for(const auto& r:results)result_bytes+=ResultBytes(r);
    }
    void Publish(StoreResult r,const Job& job){
        std::lock_guard lock(mutex);if(stop||(job.kind==KindJob::Save&&job.generation!=save_generation)||(job.kind!=KindJob::Save&&job.kind!=KindJob::Commit&&job.generation!=generation)||(job.kind==KindJob::Search&&job.search->epoch!=search_epoch)||(job.kind==KindJob::Preview&&job.preview_epoch!=preview_epoch))return;
        const size_t bytes=ResultBytes(r);if(result_bytes+bytes>SessionStore::PendingLimit){const auto id=r.entry.id;r.entry={};r.entry.id=id;r.image.reset();r.preview={};r.kind=StoreResultKind::Error;r.message=L"读取队列已满，请重试";}else result_bytes+=bytes;
        const bool signal=results.empty();results.push_back(std::move(r));if(notify&&signal)PostMessageW(notify,SessionStoreReady,0,0);
    }
    bool PreviewCurrent(const Job& job){
        std::lock_guard lock(mutex);
        return !stop&&job.generation==generation&&job.preview_epoch==preview_epoch;
    }
    void Run(const std::shared_ptr<State>& self){
        const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);bool ready=false;try{Init();Restore(self);ready=true;}catch(...){std::lock_guard lock(mutex);StoreResult result;result.message=L"历史存储无法打开，请检查权限或是否有另一实例运行";results.push_back(std::move(result));if(notify)PostMessageW(notify,SessionStoreReady,0,0);}
        {std::lock_guard lock(mutex);startup_done=true;}cv.notify_all();
        for(;;){Cleanup();Job job;{std::unique_lock lock(mutex);cv.wait(lock,[&]{return stop||!jobs.empty()||garbage_count;});if(stop)break;if(jobs.empty()){lock.unlock();Cleanup();cv.notify_all();continue;}job=std::move(jobs.front());jobs.pop_front();busy=true;}
            const size_t bytes=job.kind==KindJob::Save?job.entry.Bytes():0;StoreResult result;result.token=job.token;result.foreground_read=job.kind==KindJob::Load;result.preview_read=job.kind==KindJob::Preview;result.thumbnail_read=job.kind==KindJob::Thumb;
            try{
                Require(ready);if(job.kind==KindJob::Commit){CommitIndex(job.search->entries,job.search->groups);{std::lock_guard lock(mutex);busy=false;}cv.notify_all();continue;}if(job.kind==KindJob::Save){result.kind=StoreResultKind::Saved;result.entry=Write(std::move(job.entry),self);}
                else if(job.kind==KindJob::Search){
                    auto& task=*job.search;if(task.cursor<task.entries.size()){
                        const auto& meta=task.entries[task.cursor++];if(meta.kind!=Kind::Image||Lower(meta.text).find(task.query)!=std::wstring::npos){bool match=Lower(meta.text).find(task.query)!=std::wstring::npos;
                            if(!match&&meta.text.size()>=SessionStore::SummaryChars){try{auto e=Read(meta);match=Lower(e.text).find(task.query)!=std::wstring::npos;}catch(...){task.incomplete=true;}}
                            if(match)task.matches.push_back(meta.id);
                        }
                    }
                    if(task.cursor<task.entries.size()){std::lock_guard lock(mutex);if(!stop&&job.generation==generation&&task.epoch==search_epoch)jobs.push_back(job);busy=false;cv.notify_all();continue;}
                    result.kind=StoreResultKind::Search;result.matches=std::move(task.matches);if(task.incomplete)result.message=L"部分历史读取失败，已显示其余搜索结果";
                }else{
                    // A replaced request must not start another expensive decode.
                    if(job.kind==KindJob::Preview)Require(PreviewCurrent(job));
                    auto entry=Read(job.entry);if(job.kind==KindJob::Load){result.kind=StoreResultKind::Loaded;result.entry=std::move(entry);}
                    else if(job.kind==KindJob::Preview){Require(PreviewCurrent(job));result.kind=StoreResultKind::Preview;result.entry.id=entry.id;result.preview=PreparePreview(entry);}
                    else {result.kind=StoreResultKind::Thumbnail;result.entry.id=job.entry.id;auto source=Thumbnail(entry,128,PreviewData::MaxSourcePixels);
                        if(source){UINT w{},h{};if(SUCCEEDED(source->GetSize(&w,&h))&&w&&h&&w<=128&&h<=128){auto frame=MakeFrame({0,0,static_cast<LONG>(w),static_cast<LONG>(h)},0);if(SUCCEEDED(source->CopyPixels(nullptr,w*4,static_cast<UINT>(frame.pixels.size()*4),reinterpret_cast<BYTE*>(frame.pixels.data()))))result.image=std::make_shared<Frame>(std::move(frame));}}
                    }
                }
            }catch(...){result.kind=StoreResultKind::Error;result.entry.id=job.entry.id;result.message=job.kind==KindJob::Save?L"历史保存失败或容量已满，未记录该条":L"历史读取失败，请重试";}
            Publish(std::move(result),job);{std::lock_guard lock(mutex);pending-=bytes;busy=false;}cv.notify_all();
        }
        if(SUCCEEDED(com))CoUninitialize();
    }
};
SessionStore::SessionStore(std::filesystem::path root,HWND notify,bool persistent):state_(std::make_shared<State>(std::move(root),notify,persistent)){auto state=state_;state->worker=std::thread([state]{state->Run(state);});}
SessionStore::~SessionStore(){auto s=state_;if(s->persistent)WaitIdle();{std::lock_guard lock(s->mutex);s->stop=true;s->jobs.clear();s->results.clear();}s->cv.notify_all();if(s->worker.joinable())s->worker.join();if(s->lease.h!=INVALID_HANDLE_VALUE){CloseHandle(s->lease.h);s->lease.h=INVALID_HANDLE_VALUE;}if(!s->persistent&&!s->directory.empty()){std::error_code ec;std::filesystem::remove_all(s->directory,ec);}}
void SessionStore::Commit(std::vector<Entry> entries,std::vector<Group> groups){auto s=state_;if(!s->persistent)return;auto snapshot=std::make_shared<State::SearchTask>();snapshot->entries=std::move(entries);snapshot->groups=std::move(groups);std::lock_guard lock(s->mutex);if(s->stop)return;std::erase_if(s->jobs,[](const State::Job& job){return job.kind==State::KindJob::Commit;});s->jobs.push_back({State::KindJob::Commit,{},0,s->generation,std::move(snapshot)});s->cv.notify_all();}
bool SessionStore::Save(Entry entry){auto s=state_;const auto bytes=entry.Bytes();std::lock_guard lock(s->mutex);if(s->stop||bytes>History::MaxItemBytes||entry.payload||entry.formats.empty()||s->pending+bytes>PendingLimit||s->jobs.size()>=32||s->results.size()>=64)return false;s->pending+=bytes;s->jobs.push_back({State::KindJob::Save,std::move(entry),0,s->save_generation,{}});s->cv.notify_all();return true;}
bool SessionStore::Load(const Entry& entry,uint64_t token,bool thumbnail){auto s=state_;std::lock_guard lock(s->mutex);if(s->stop||!entry.payload||s->jobs.size()>=32)return false;if(!thumbnail)std::erase_if(s->jobs,[](const State::Job& job){return job.kind==State::KindJob::Load;});State::Job job{thumbnail?State::KindJob::Thumb:State::KindJob::Load,entry,token,s->generation,{}};if(thumbnail)s->jobs.push_back(std::move(job));else s->jobs.push_front(std::move(job));s->cv.notify_all();return true;}
bool SessionStore::LoadPreview(const Entry& entry,uint64_t token){
    auto s=state_;std::lock_guard lock(s->mutex);s->DropPreviewLocked();
    if(s->stop||!entry.payload||s->jobs.size()>=32)return false;
    s->jobs.push_front({State::KindJob::Preview,entry,token,s->generation,{},s->preview_epoch});
    s->cv.notify_all();return true;
}
void SessionStore::CancelPreview(){auto s=state_;std::lock_guard lock(s->mutex);s->DropPreviewLocked();s->cv.notify_all();}
void SessionStore::Search(std::vector<Entry> entries,std::wstring query,uint64_t token){auto s=state_;auto task=std::make_shared<State::SearchTask>();task->entries=std::move(entries);task->query=Lower(std::move(query));std::lock_guard lock(s->mutex);task->epoch=++s->search_epoch;std::erase_if(s->jobs,[](const State::Job& job){return job.kind==State::KindJob::Search;});s->jobs.push_back({State::KindJob::Search,{},token,s->generation,std::move(task)});s->cv.notify_all();}
void SessionStore::CancelSaves(){auto s=state_;std::lock_guard lock(s->mutex);++s->save_generation;
    std::erase_if(s->jobs,[&](const State::Job& job){if(job.kind!=State::KindJob::Save)return false;s->pending-=job.entry.Bytes();return true;});
    std::erase_if(s->results,[](const StoreResult& result){return result.kind==StoreResultKind::Saved||(result.kind==StoreResultKind::Error&&result.token==0);});s->cv.notify_all();}
void SessionStore::CancelReads(){auto s=state_;std::lock_guard lock(s->mutex);++s->generation;++s->search_epoch;std::erase_if(s->jobs,[](const State::Job& job){return job.kind!=State::KindJob::Save&&job.kind!=State::KindJob::Commit;});std::erase_if(s->results,[](const StoreResult& r){return r.kind!=StoreResultKind::Saved&&r.kind!=StoreResultKind::Restored;});s->result_bytes=0;s->cv.notify_all();}
std::vector<StoreResult> SessionStore::Take(){auto s=state_;std::lock_guard lock(s->mutex);std::vector<StoreResult> results;results.swap(s->results);s->result_bytes=0;return results;}
bool SessionStore::WaitIdle(unsigned ms){auto s=state_;std::unique_lock lock(s->mutex);return s->cv.wait_for(lock,std::chrono::milliseconds(ms),[&]{return s->startup_done&&s->jobs.empty()&&!s->busy&&s->garbage_count==0;});}
StoreStats SessionStore::Stats(){auto s=state_;std::lock_guard lock(s->mutex);return {s->pending,s->result_bytes,s->disk_bytes.load(),s->jobs.size()};}
bool SessionStore::PurgePersistent(const std::filesystem::path& root){try{return PurgeHistoryDirectory(root/L"history-v1");}catch(...){return false;}}
std::filesystem::path SessionStore::Directory(){auto s=state_;std::lock_guard lock(s->mutex);return s->directory;}
}