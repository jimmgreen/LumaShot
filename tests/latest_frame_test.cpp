#include "recording/latest_frame.h"
#include <array>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void Expect(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct State {std::array<int,4> closes{};};
struct Frame {
    State* state{};int id{};
    Frame(std::nullptr_t){}
    Frame(State& owner,int index):state(&owner),id(index){}
    Frame(const Frame&)=delete;
    Frame& operator=(const Frame&)=delete;
    Frame(Frame&& other)noexcept:state(std::exchange(other.state,nullptr)),id(other.id){}
    Frame& operator=(Frame&& other)noexcept{state=std::exchange(other.state,nullptr);id=other.id;return *this;}
    explicit operator bool()const{return state!=nullptr;}
    void Close(){++state->closes[size_t(id)];}
};
void Run(int total,int invalid=-1,bool consume_throws=false,bool acquire_throws=false){
    State state;int acquired=0,validated=0,consumed=0;bool threw=false;
    try{
        lumashot::recording::ConsumeLatestFrame([&]()->Frame{
            // Before acquiring the third lease, the first lease must be released.
            if(acquired>=2)Expect(state.closes[size_t(acquired-2)]==1,"superseded frame was retained");
            if(acquire_throws&&acquired==total)throw std::runtime_error("acquire");
            if(acquired==total)return Frame{nullptr};
            return Frame{state,acquired++};
        },[&](const Frame& frame){
            ++validated;Expect(state.closes[size_t(frame.id)]==0,"closed before validation");
            if(frame.id==invalid)throw std::runtime_error("size");
        },[&](const Frame& frame){
            ++consumed;Expect(frame.id==total-1,"consumed an obsolete frame");
            Expect(state.closes[size_t(frame.id)]==0,"newest closed before copy");
            for(int i=0;i<frame.id;++i)Expect(state.closes[size_t(i)]==1,"old lease still open during copy");
            if(consume_throws)throw std::runtime_error("copy");
        });
    }catch(const std::runtime_error& error){
        const std::string message=error.what();
        if(message!="size"&&message!="copy"&&message!="acquire")throw;
        threw=true;
    }
    Expect(threw==(invalid>=0||consume_throws||acquire_throws),"exception propagation");
    Expect(validated==acquired,"all acquired frames must be validated");
    Expect(consumed==((total&&invalid<0&&!acquire_throws)?1:0),"consume count");
    for(int i=0;i<acquired;++i)Expect(state.closes[size_t(i)]==1,"lease not closed exactly once");
}
}
int main(){
    try{Run(0);Run(1);Run(4);Run(4,0);Run(4,2);Run(4,3);Run(4,-1,true);Run(2,-1,false,true);
        std::cout<<"PASS newest-frame drain, lease lifetime, resize and failure cleanup\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
