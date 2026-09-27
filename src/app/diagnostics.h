#pragma once
#include <windows.h>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <mutex>
namespace lumashot {
// Opt-in numeric diagnostics: no images, text, window titles or clipboard data.
class Diagnostics {
    struct Entry {const char* name;double start,ms;long long value;};
    std::vector<Entry> entries_;
    std::wstring path_;
    std::mutex mutex_;
public:
    static Diagnostics& Get(){static Diagnostics instance;return instance;}
    Diagnostics(){wchar_t path[32768]{};const DWORD n=GetEnvironmentVariableW(L"LUMASHOT_TRACE",path,32768);if(n&&n<32768){path_=path;entries_.reserve(100000);}}
    bool Enabled()const{return !path_.empty();}
    static double Now(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
    void Add(const char* name,double start,long long value=0){if(!Enabled())return;std::lock_guard lock(mutex_);if(entries_.size()<100000)entries_.push_back({name,start,Now()-start,value});}
    void Flush(){if(!Enabled())return;std::lock_guard lock(mutex_);std::ofstream file{std::filesystem::path(path_)};file<<"event,start_ms,duration_ms,value\n";file.precision(12);for(const auto& e:entries_)file<<e.name<<','<<e.start<<','<<e.ms<<','<<e.value<<'\n';}
};
struct TraceScope {
    const char* name;double start{};long long value;
    explicit TraceScope(const char* n,long long v=0):name(n),value(v){if(Diagnostics::Get().Enabled())start=Diagnostics::Now();}
    ~TraceScope(){if(start)Diagnostics::Get().Add(name,start,value);}
};
}
