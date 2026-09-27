#pragma once
#include <array>
#include <algorithm>
#include <span>
#include <cstdint>
#include <climits>
#include <stdexcept>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
namespace lumashot::recording {
// Exact weighted nearest-color search. No coordinate-dependent noise and no
// 5-bit RGB rounding: identical source colors always produce identical pixels.
class GifQuantizer {
    struct Node {std::array<int,3> rgb{};int index{},axis{},left{-1},right{-1};};
    std::array<Node,255> nodes_{};
    int root_{-1};
    static constexpr std::array<int,3> weights_{2,4,1};
    int Build(int begin,int end,int depth){
        if(begin==end)return -1;
        const int axis=depth%3,mid=(begin+end)/2;
        std::nth_element(nodes_.begin()+begin,nodes_.begin()+mid,nodes_.begin()+end,
            [axis](const Node& a,const Node& b){return a.rgb[axis]<b.rgb[axis];});
        nodes_[mid].axis=axis;
        nodes_[mid].left=Build(begin,mid,depth+1);
        nodes_[mid].right=Build(mid+1,end,depth+1);
        return mid;
    }
    void Search(int node,const std::array<int,3>& rgb,int& best,int& distance)const noexcept{
        if(node<0)return;
        const auto& n=nodes_[node];int error=0;
        for(int c=0;c<3;++c){const int d=rgb[c]-n.rgb[c];error+=weights_[c]*d*d;}
        if(error<distance||(error==distance&&n.index<best)){distance=error;best=n.index;}
        const int delta=rgb[n.axis]-n.rgb[n.axis];
        Search(delta<0?n.left:n.right,rgb,best,distance);
        if(weights_[n.axis]*delta*delta<=distance)Search(delta<0?n.right:n.left,rgb,best,distance);
    }
    void MapRange(const uint8_t* bgra,size_t begin,size_t end,uint8_t* output)const noexcept{
        uint32_t previous=UINT32_MAX;uint8_t mapped=0;
        for(size_t pos=begin;pos<end;++pos){const auto* p=bgra+pos*4;
            const uint32_t color=(uint32_t(p[2])<<16)|(uint32_t(p[1])<<8)|p[0];
            if(color!=previous){int best=INT_MAX,distance=INT_MAX;Search(root_,{p[2],p[1],p[0]},best,distance);mapped=uint8_t(best);previous=color;}
            output[pos]=mapped;
        }
    }
    // Three persistent workers plus the calling export thread. Each stripe has
    // its own previous-color cache; palette lookup and tie breaking stay exact.
    struct Workers {
        const GifQuantizer& owner;
        std::array<std::thread,3> threads;
        std::mutex mutex;
        std::condition_variable ready,done;
        const uint8_t* source{};
        uint8_t* destination{};
        size_t width{},height{},generation{},remaining{};
        unsigned count{};
        bool stopping{};
        explicit Workers(const GifQuantizer& quantizer,unsigned workerCount):owner(quantizer),count(workerCount){
            try{for(unsigned i=0;i<count;++i)threads[i]=std::thread([this,i]{Run(i);});}
            catch(...){Stop();throw;}
        }
        ~Workers(){Stop();}
        void Stop(){
            {std::lock_guard lock(mutex);stopping=true;}
            ready.notify_all();
            for(auto& thread:threads)if(thread.joinable())thread.join();
        }
        void Run(unsigned stripe){
            size_t seen=0;
            std::unique_lock lock(mutex);
            for(;;){
                ready.wait(lock,[&]{return stopping||generation!=seen;});
                if(stopping)return;
                seen=generation;
                const auto* input=source;auto* output=destination;
                const size_t begin=(height*stripe/(count+1))*width;
                const size_t end=(height*(stripe+1)/(count+1))*width;
                lock.unlock();owner.MapRange(input,begin,end,output);lock.lock();
                if(--remaining==0)done.notify_one();
            }
        }
        void Map(const uint8_t* input,int w,int h,uint8_t* output){
            {std::lock_guard lock(mutex);
                source=input;destination=output;width=size_t(w);height=size_t(h);remaining=count;++generation;
            }
            ready.notify_all();
            owner.MapRange(input,(size_t(h)*count/(count+1))*size_t(w),size_t(w)*size_t(h),output);
            std::unique_lock lock(mutex);done.wait(lock,[&]{return remaining==0;});
        }
    };
    mutable std::mutex mapMutex_;
    mutable std::unique_ptr<Workers> workers_;
public:
    explicit GifQuantizer(std::span<const uint32_t> palette){
        if(palette.empty()||palette.size()>nodes_.size())throw std::invalid_argument("Invalid GIF palette");
        for(size_t i=0;i<palette.size();++i){nodes_[i].rgb={int((palette[i]>>16)&255),int((palette[i]>>8)&255),int(palette[i]&255)};nodes_[i].index=int(i);}
        root_=Build(0,int(palette.size()),0);
    }
    void MapSerial(const uint8_t* bgra,int width,int height,uint8_t* output)const noexcept{
        if(width<=0||height<=0)return;
        MapRange(bgra,0,size_t(width)*size_t(height),output);
    }
    void Map(const uint8_t* bgra,int width,int height,uint8_t* output)const{
        if(width<=0||height<=0)return;
        if(size_t(width)*size_t(height)<128*1024||height<4){MapSerial(bgra,width,height,output);return;}
        std::lock_guard lock(mapMutex_);
        if(!workers_){
            const unsigned concurrency=std::thread::hardware_concurrency();
            if(concurrency<2){MapSerial(bgra,width,height,output);return;}
            workers_=std::make_unique<Workers>(*this,std::min(3u,concurrency-1));
        }
        workers_->Map(bgra,width,height,output);
    }
};
}
