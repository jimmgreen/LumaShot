#pragma once
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <array>
#include <iterator>

namespace lumashot::clipboard {
enum class Kind { Text, Image, Files };
struct Format { UINT id{};std::vector<unsigned char> bytes;bool operator==(const Format&)const=default; };
struct DiskPayload {
    uint64_t key{};size_t bytes{};std::string digest;
    std::function<void(uint64_t)> release;
    ~DiskPayload(){if(release)release(key);}
};
// User-defined, named group. Entry::group refers to Group::id (0 = no group).
struct Group {
    uint32_t id{};std::wstring name;uint32_t color{};
    bool operator==(const Group&)const=default;
};
struct Entry {
    uint64_t id{};Kind kind{};std::wstring text;std::vector<Format> formats;
    SYSTEMTIME time{};bool favorite{};uint32_t group{};uint32_t file_count{};std::array<std::wstring,3> file_names;std::shared_ptr<const DiskPayload> payload;
    size_t Bytes()const {if(payload)return payload->bytes;size_t n=text.size()*sizeof(wchar_t);for(const auto& f:formats)n+=f.bytes.size();return n;}
};
// Capacity model: ordinary entries roll over at MaxEntries. Favorites and
// grouped entries are "protected": they are never evicted, do not consume the
// ordinary budget and are capped separately at MaxProtected. MaxBytes bounds the
// logical payload of everything together; protected entries can never push it
// over, so a new ordinary capture is only rejected when it alone cannot fit.
class History {
public:
    static constexpr size_t MaxEntries=100,MaxProtected=200,MaxTotal=MaxEntries+MaxProtected;
    static constexpr size_t MaxBytes=128*1024*1024,MaxItemBytes=16*1024*1024;
    static constexpr size_t MaxGroups=20,MaxGroupName=12;
    static constexpr int GroupTabBase=5;  // tabs 0..4 are 全部/文本/图片/文件/收藏
    static constexpr uint32_t Palette[8]={0x2f7cf6,0xe0703a,0x2fa36b,0x9b59d0,0xd14f7a,0xc79a12,0x1aa3b8,0x6b7a90};
    std::vector<Entry> entries;std::vector<Group> groups;
    static bool Protected(const Entry& e){return e.favorite||e.group!=0;}
    bool Add(Entry item){
        if((item.formats.empty()&&!item.payload)||item.Bytes()>MaxItemBytes)return false;
        if(item.group&&!FindGroup(item.group))item.group=0;
        auto duplicate=std::find_if(entries.begin(),entries.end(),[&](const Entry& e){return e.payload||item.payload ? e.payload&&item.payload&&e.payload->digest==item.payload->digest : e.formats==item.formats;});
        const bool replacing=duplicate!=entries.end();
        if(replacing){item.id=duplicate->id;item.favorite=duplicate->favorite;item.group=duplicate->group;}
        // Check capacity before evicting anything: a rejected capture must not
        // destroy unrelated history when protected entries consume the budget.
        size_t protected_bytes=0,protected_count=0;
        for(auto it=entries.begin();it!=entries.end();++it)if(it!=duplicate&&Protected(*it)){protected_bytes+=it->Bytes();++protected_count;}
        if(protected_bytes+item.Bytes()>MaxBytes)return false;
        if(Protected(item)&&protected_count>=MaxProtected)return false;
        entries.reserve(MaxTotal);
        if(replacing){
            std::erase_if(entries,[&](const Entry& e){return e.id==item.id;});
        }else item.id=++next_;
        const size_t incoming=Protected(item)?0:1;
        while(OrdinaryCount()+incoming>MaxEntries||Bytes()+item.Bytes()>MaxBytes){
            auto last=std::find_if(entries.rbegin(),entries.rend(),[](const Entry& e){return !Protected(e);});
            if(last==entries.rend())break;
            entries.erase(std::next(last).base());
        }
        entries.insert(entries.begin(),std::move(item));return true;
    }
    size_t Bytes()const {size_t n=0;for(const auto& e:entries)n+=e.Bytes();return n;}
    size_t ProtectedCount()const {return static_cast<size_t>(std::count_if(entries.begin(),entries.end(),[](const Entry& e){return Protected(e);}));}
    size_t OrdinaryCount()const {return entries.size()-ProtectedCount();}
    // Whether e may become protected (favorited or grouped) without exceeding the cap.
    bool CanProtect(const Entry& e)const {return Protected(e)||ProtectedCount()<MaxProtected;}
    Entry* Find(uint64_t id){auto i=std::find_if(entries.begin(),entries.end(),[&](const Entry& e){return e.id==id;});return i==entries.end()?nullptr:&*i;}
    void Erase(uint64_t id){std::erase_if(entries,[&](const Entry& e){return e.id==id;});}
    // keep_protected keeps favorites and grouped entries.
    void Clear(bool keep_protected){std::erase_if(entries,[&](const Entry& e){return !keep_protected||!Protected(e);});}
    const Group* FindGroup(uint32_t id)const {auto i=std::find_if(groups.begin(),groups.end(),[&](const Group& g){return g.id==id;});return i==groups.end()?nullptr:&*i;}
    int GroupIndex(uint32_t id)const {for(size_t i=0;i<groups.size();++i)if(groups[i].id==id)return static_cast<int>(i);return -1;}
    static std::wstring CleanGroupName(std::wstring name){
        std::erase_if(name,[](wchar_t c){return c<32||c==0x7f;});
        const auto first=name.find_first_not_of(L" \t\x3000"),last=name.find_last_not_of(L" \t\x3000");
        name=first==std::wstring::npos?std::wstring{}:name.substr(first,last-first+1);
        if(name.size()>MaxGroupName){name.resize(MaxGroupName);if(name.back()>=0xd800&&name.back()<=0xdbff)name.pop_back();}
        return name;
    }
    bool GroupNameTaken(const std::wstring& name,uint32_t except=0)const {return std::any_of(groups.begin(),groups.end(),[&](const Group& g){return g.id!=except&&g.name==name;});}
    // Returns the new group id, or 0 when the name is empty/duplicate or the group cap is reached.
    uint32_t AddGroup(std::wstring name){
        name=CleanGroupName(std::move(name));
        if(name.empty()||groups.size()>=MaxGroups||GroupNameTaken(name))return 0;
        uint32_t id=1;for(const auto& g:groups)id=std::max(id,g.id+1);
        groups.push_back({id,std::move(name),Palette[(id-1)%std::size(Palette)]});return id;
    }
    bool RenameGroup(uint32_t id,std::wstring name){
        name=CleanGroupName(std::move(name));
        auto i=std::find_if(groups.begin(),groups.end(),[&](const Group& g){return g.id==id;});
        if(i==groups.end()||name.empty()||GroupNameTaken(name,id))return false;
        i->name=std::move(name);return true;
    }
    void CycleGroupColor(uint32_t id){
        for(auto& g:groups)if(g.id==id){const auto* at=std::find(std::begin(Palette),std::end(Palette),g.color);
            g.color=Palette[at==std::end(Palette)?0:(static_cast<size_t>(at-std::begin(Palette))+1)%std::size(Palette)];}
    }
    // Deleting a group never deletes entries; they return to ordinary history.
    // Nothing is evicted here: the ordinary budget is re-applied by the next Add,
    // so releasing entries can never destroy the entries just released.
    void RemoveGroup(uint32_t id){
        std::erase_if(groups,[&](const Group& g){return g.id==id;});
        for(auto& e:entries)if(e.group==id)e.group=0;
    }
    // Moves an entry into a group (0 removes it). Fails when the protected cap is reached.
    bool SetGroup(uint64_t entry,uint32_t group){
        auto* e=Find(entry);if(!e||(group&&!FindGroup(group)))return false;
        if(group&&!e->group&&!CanProtect(*e))return false;
        e->group=group;return true;
    }
    // Restore helper: replaces the group table and drops dangling references.
    void SetGroups(std::vector<Group> value){
        groups.clear();
        for(auto& g:value){g.name=CleanGroupName(std::move(g.name));if(g.id&&!g.name.empty()&&groups.size()<MaxGroups&&!FindGroup(g.id)&&!GroupNameTaken(g.name))groups.push_back(std::move(g));}
        for(auto& e:entries)if(e.group&&!FindGroup(e.group))e.group=0;
    }
    void TrimOrdinary(){
        while(OrdinaryCount()>MaxEntries){
            auto last=std::find_if(entries.rbegin(),entries.rend(),[](const Entry& e){return !Protected(e);});
            if(last==entries.rend())break;
            entries.erase(std::next(last).base());
        }
    }
    std::vector<uint64_t> Filter(int tab,const std::wstring& query,bool oldest=false)const {
        auto lower=[](std::wstring s){for(auto& c:s)c=static_cast<wchar_t>(std::towlower(c));return s;};
        const uint32_t group=tab>=GroupTabBase&&static_cast<size_t>(tab-GroupTabBase)<groups.size()?groups[static_cast<size_t>(tab-GroupTabBase)].id:0;
        std::vector<uint64_t> result;const auto needle=lower(query);
        for(const auto& e:entries){if(tab==1&&e.kind!=Kind::Text)continue;if(tab==2&&e.kind!=Kind::Image)continue;if(tab==3&&e.kind!=Kind::Files)continue;if(tab==4&&!e.favorite)continue;
            if(tab>=GroupTabBase&&(!group||e.group!=group))continue;
            if(!needle.empty()&&lower(e.text).find(needle)==std::wstring::npos)continue;result.push_back(e.id);}
        if(oldest)std::reverse(result.begin(),result.end());
        std::stable_partition(result.begin(),result.end(),[&](uint64_t id){
            return std::find_if(entries.begin(),entries.end(),[&](const Entry& e){return e.id==id;})->favorite;
        });
        return result;
    }
private:uint64_t next_{};
};
}