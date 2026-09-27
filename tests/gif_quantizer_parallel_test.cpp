#include "recording/gif_quantizer.h"
#include <chrono>
#include <iostream>
#include <vector>

using lumashot::recording::GifQuantizer;
namespace {
int failures{};
void Expect(bool condition,const char* label){
    std::cout<<(condition?"PASS ":"FAIL ")<<label<<'\n';
    failures+=!condition;
}
std::vector<uint8_t> Pixels(int width,int height,unsigned frame){
    std::vector<uint8_t> pixels(size_t(width)*size_t(height)*4);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        const size_t p=(size_t(y)*size_t(width)+size_t(x))*4;
        pixels[p]=uint8_t((unsigned(x)*255/unsigned(width)+frame*7)%256);
        pixels[p+1]=uint8_t((unsigned(y)*255/unsigned(height)+frame*13)%256);
        pixels[p+2]=uint8_t((unsigned(x+y)+frame*19)%256);
        pixels[p+3]=uint8_t(frame);
        // Long identical runs across row/stripe boundaries exercise local caches.
        if(y%31<4){pixels[p]=73;pixels[p+1]=91;pixels[p+2]=127;}
    }
    return pixels;
}
void Compare(const GifQuantizer& quantizer,int width,int height,unsigned frame){
    auto pixels=Pixels(width,height,frame);
    std::vector<uint8_t> serial(size_t(width)*size_t(height));
    // Guard bytes also check the last uneven stripe stops exactly at the end.
    std::vector<uint8_t> parallel(serial.size()+2,254);
    quantizer.MapSerial(pixels.data(),width,height,serial.data());
    quantizer.Map(pixels.data(),width,height,parallel.data()+1);
    Expect(std::equal(serial.begin(),serial.end(),parallel.begin()+1),"serial and parallel indices match byte for byte");
    Expect(parallel.front()==254&&parallel.back()==254,"stripe output stays within bounds");
}
void Benchmark(const GifQuantizer& quantizer,int width,int height,bool flat){
    constexpr int iterations=8;
    auto pixels=Pixels(width,height,11);
    if(flat)for(size_t p=0;p<pixels.size();p+=4){pixels[p]=73;pixels[p+1]=91;pixels[p+2]=127;}
    std::vector<uint8_t> serial(size_t(width)*size_t(height)),parallel(serial.size());
    quantizer.Map(pixels.data(),width,height,parallel.data());
    quantizer.MapSerial(pixels.data(),width,height,serial.data());
    double serialMs=0,parallelMs=0;
    bool exact=true;
    // Batch fast flat-color frames so timer and scheduling noise do not dominate.
    const int batch=flat?20:1;
    for(int i=0;i<iterations;++i){
        const auto measure=[&](bool useParallel){
            const auto start=std::chrono::steady_clock::now();
            for(int repeat=0;repeat<batch;++repeat){
                if(useParallel)quantizer.Map(pixels.data(),width,height,parallel.data());
                else quantizer.MapSerial(pixels.data(),width,height,serial.data());
            }
            return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        };
        if(i%2){parallelMs+=measure(true);serialMs+=measure(false);}
        else{serialMs+=measure(false);parallelMs+=measure(true);}
        exact=exact&&serial==parallel;
    }
    Expect(exact,"benchmark outputs remain exact");
    std::cout<<width<<'x'<<height<<(flat?" flat":" gradient")<<", "<<iterations*batch
             <<" frames: serial="<<serialMs<<" ms parallel="<<parallelMs
             <<" ms speedup="<<serialMs/parallelMs<<"x\n";
}
}
int main(){
    try{
        std::array<uint32_t,255> palette{};
        for(size_t i=0;i<palette.size();++i)
            palette[i]=uint32_t(((i*37)%256)<<16|((i*73)%256)<<8|((i*19)%256));
        palette[254]=palette[0];
        GifQuantizer quantizer(palette);
        Compare(quantizer,1,1,0);Compare(quantizer,127,31,1);
        Compare(quantizer,512,255,2);Compare(quantizer,512,256,3);
        Compare(quantizer,511,259,4);Compare(quantizer,640,360,5);
        Compare(quantizer,1023,259,4);Compare(quantizer,100001,3,5);
        for(unsigned frame=0;frame<8;++frame)Compare(quantizer,1281,721,frame);
        const std::array<uint32_t,3> tied{0,0x00000002,0};
        GifQuantizer ties(tied);
        std::vector<uint8_t> flat(1025*257*4,0),mapped(1025*257);
        for(size_t p=0;p<mapped.size();++p)flat[p*4]=1;
        ties.Map(flat.data(),1025,257,mapped.data());
        Expect(std::all_of(mapped.begin(),mapped.end(),[](uint8_t p){return p==0;}),"equidistant and duplicate colors retain lowest palette index");
        uint8_t guard=123;
        quantizer.Map(nullptr,0,10,&guard);quantizer.Map(nullptr,10,0,&guard);
        Expect(guard==123,"empty frames leave output untouched");

        Benchmark(quantizer,640,360,false);Benchmark(quantizer,640,360,true);
        Benchmark(quantizer,1920,1080,false);Benchmark(quantizer,1920,1080,true);
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';++failures;}
    return failures?1:0;
}
