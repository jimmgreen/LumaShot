#include "clipboard/history.h"
#include <iostream>
using namespace lumashot::clipboard;
int main(){
    int failures=0;auto expect=[&](bool ok,const char* what){std::cout<<(ok?"PASS ":"FAIL ")<<what<<'\n';failures+=!ok;};
    auto entry=[](int n){Entry e;e.kind=Kind::Text;e.text=L"合成 Text "+std::to_wstring(n);e.formats.push_back({CF_UNICODETEXT,{static_cast<unsigned char>(n),static_cast<unsigned char>(n>>8)}});return e;};
    History h;expect(h.entries.empty(),"starts empty without reading OS clipboard");expect(h.Add(entry(1)),"add synthetic text");const auto id=h.entries[0].id;h.entries[0].favorite=true;
    expect(h.Add(entry(1))&&h.entries.size()==1&&h.entries[0].favorite&&h.entries[0].id==id,"deduplicate and preserve favorite identity");
    for(int i=2;i<160;++i)h.Add(entry(i));expect(h.entries.size()==101&&h.OrdinaryCount()==History::MaxEntries&&h.Find(id),"ordinary entries roll at 100 while favorite is kept outside the budget");
    expect(h.Filter(1,L"TEXT 159").size()==1,"case-insensitive text search");expect(h.Filter(2,L"").empty(),"image category excludes text");expect(h.Filter(4,L"").size()==1,"favorite category");
    const auto forward=h.Filter(0,L""),reverse=h.Filter(0,L"",true);expect(forward.front()==id&&reverse.front()==id&&forward[1]==reverse.back(),"pinned entries stay first in both time orders");
    auto image=entry(200);image.kind=Kind::Image;h.Add(image);expect(h.Filter(2,L"").size()==1,"image filter");
    auto files=entry(201);files.kind=Kind::Files;h.Add(files);expect(h.Filter(3,L"").size()==1,"file filter");
    h.Clear(true);expect(h.entries.size()==1&&h.Find(id),"clear unpinned retains favorites");h.Erase(id);expect(h.entries.empty(),"delete entry");
    auto huge=entry(0);huge.formats[0].bytes.resize(History::MaxItemBytes+1);expect(!h.Add(std::move(huge))&&h.entries.empty(),"reject oversized item");
    for(int i=0;i<static_cast<int>(History::MaxProtected);++i){auto e=entry(1000+i);e.favorite=true;h.Add(e);}
    {auto extra=entry(1500);extra.favorite=true;expect(h.ProtectedCount()==History::MaxProtected&&!h.Add(extra),"protected area caps at 200");}
    expect(h.Add(entry(500))&&h.entries.size()==History::MaxProtected+1,"full protected area still accepts ordinary copies");
    expect(!h.CanProtect(h.entries.front()),"full protected area refuses new favorite or group");h.Clear(false);expect(h.Bytes()==0,"disable clears memory");
    History capacity;
    auto stored=[](int n,size_t bytes,bool pin){Entry e;e.text=std::to_wstring(n);e.favorite=pin;auto payload=std::make_shared<DiskPayload>();payload->key=n;payload->bytes=bytes;payload->digest=std::to_string(n);e.payload=payload;return e;};
    for(int i=1;i<=8;++i)expect(capacity.Add(stored(i,15*1024*1024,true)),"fill protected payload budget");
    expect(capacity.Add(stored(105,1024*1024,false)),"retain ordinary entry beside protected entries");
    const auto ordinary=capacity.entries.front().id;
    expect(!capacity.Add(stored(106,9*1024*1024,false))&&capacity.Find(ordinary)&&capacity.entries.size()==9,"failed admission cannot evict unrelated history");
    History order;for(int i=1;i<=12;++i)order.Add(entry(i));
    const auto oldest_id=order.entries.back().id;order.Find(oldest_id)->favorite=true;
    expect(order.Filter(0,L"").front()==oldest_id&&order.Filter(1,L"Text").front()==oldest_id,"pin leads general, category and search results");
    order.Add(entry(50));expect(order.Filter(0,L"").front()==oldest_id,"new copy cannot displace pinned entry");
    order.Find(oldest_id)->favorite=false;expect(order.Filter(0,L"").back()==oldest_id,"unpin restores chronological position");
    // Custom groups.
    History g;for(int i=1;i<=5;++i)g.Add(entry(2000+i));
    const auto work=g.AddGroup(L"  工作  ");expect(work!=0&&g.groups.size()==1&&g.groups[0].name==L"工作","add group trims name");
    expect(g.AddGroup(L"工作")==0&&g.AddGroup(L"   ")==0,"reject duplicate or empty group name");
    expect(History::CleanGroupName(L"一二三四五六七八九十甲乙丙丁").size()==History::MaxGroupName,"group name is length-limited");
    const auto code=g.AddGroup(L"代码");expect(code&&g.groups[1].color!=g.groups[0].color,"new groups get distinct palette colors");
    const auto first=g.entries[0].id,second=g.entries[1].id;
    expect(g.SetGroup(first,work)&&g.SetGroup(second,code),"assign entries to groups");
    expect(g.Filter(History::GroupTabBase,L"").size()==1&&g.Filter(History::GroupTabBase,L"")[0]==first,"group tab shows only its entries");
    expect(g.Filter(History::GroupTabBase+1,L"2005").empty()&&g.Filter(History::GroupTabBase+1,L"").size()==1,"search is scoped to the group tab");
    expect(g.Filter(History::GroupTabBase+9,L"").empty(),"unknown group tab is empty");
    g.Find(first)->favorite=true;expect(g.Find(first)->group==work&&g.ProtectedCount()==2,"entry can be grouped and favorite and counts once");
    {auto again=entry(2005);expect(g.Add(again)&&g.entries[0].id==first&&g.entries[0].group==work,"recopy keeps group and identity");}
    for(int i=0;i<150;++i)g.Add(entry(3000+i));expect(g.Find(first)&&g.Find(second)&&g.OrdinaryCount()==History::MaxEntries,"grouped entries survive ordinary rollover");
    g.Clear(true);expect(g.entries.size()==2,"clear keeps grouped and favorite entries");
    expect(g.RenameGroup(code,L"片段")&&g.groups[1].name==L"片段"&&!g.RenameGroup(code,L"工作"),"rename group rejects duplicates");
    {const auto before=g.groups[1].color;g.CycleGroupColor(code);expect(g.groups[1].color!=before,"cycle group color");for(int i=0;i<7;++i)g.CycleGroupColor(code);expect(g.groups[1].color==before,"color cycles through 8 palette entries");}
    g.RemoveGroup(code);expect(g.groups.size()==1&&g.Find(second)&&g.Find(second)->group==0,"deleting a group keeps its entries in 全部");
    expect(g.SetGroup(first,0)&&g.Find(first)->group==0&&g.Find(first)->favorite,"remove from group keeps favorite");
    expect(!g.SetGroup(first,999),"reject unknown group id");
    History many;for(size_t i=0;i<History::MaxGroups;++i)expect(many.AddGroup(L"组"+std::to_wstring(i))!=0,"add up to 20 groups");
    expect(many.AddGroup(L"多余")==0,"group count caps at 20");
    History restore;restore.SetGroups({{7,L"A",History::Palette[1]},{7,L"B",0},{0,L"C",0},{9,L"A",0}});expect(restore.groups.size()==1&&restore.groups[0].id==7,"restore drops invalid or duplicate groups");
    {auto e=entry(4000);e.group=42;expect(restore.Add(e)&&restore.entries[0].group==0,"dangling group id is dropped on add");}
    History full;for(int i=0;i<static_cast<int>(History::MaxProtected);++i){auto e=entry(5000+i);e.favorite=true;full.Add(e);}
    const auto box=full.AddGroup(L"盒子");full.Add(entry(6000));expect(!full.SetGroup(full.entries[0].id,box)&&full.entries[0].group==0,"full protected area refuses move into group");
    expect(full.SetGroup(full.entries[1].id,box),"favorite already protected can still join a group");
    return failures?1:0;
}