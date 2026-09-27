#include "gif_test_helpers.h"
#include "recording/gif_optimizer.h"
#include "recording/gif_quantizer.h"
#include "recording/gif_palette.h"
#include "recording/storage.h"
#include <iostream>
#include <thread>
using namespace lumashot::recording;
using namespace gif_test;
namespace {int failures{};void Expect(bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<std::endl;failures+=!ok;}}
int main(){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    try{
        const auto folder=std::filesystem::current_path()/L"gif-pipeline-fixture";std::filesystem::create_directories(folder);
        ComPtr<IWICImagingFactory> f;Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&f)),"Factory");
        const std::array<uint32_t,4> colors{0xfff7f7f7,0xff171717,0xff185bd1,0xffffa010};
        constexpr int w=128,h=72;std::vector<BYTE> a(w*h,0),b,c;
        for(int y=8;y<45;++y)for(int x=8;x<70;++x)if((x%7==0)||(y%9==0&&x%7<4))a[size_t(y)*w+x]=1;
        b=a;for(int y=48;y<58;++y)for(int x=4;x<12;++x)b[size_t(y)*w+x]=2;
        b[60*w+120]=3;c=a;c[60*w+120]=3;
        const auto raw=folder/L"raw.gif";
        {GifFrames frames(f.Get(),raw,w,h,colors,true,{});frames.Add(a,10);frames.Add(a,20);frames.Add(b,10);frames.Add(c,10);frames.Finish();}
        std::vector<std::vector<BYTE>> expected;std::vector<unsigned> delays;
        const auto info=Decode(raw,[&](const auto& canvas,unsigned delay,UINT i){
            const auto& indices=i==0?a:i==1?b:c;bool equal=true;
            for(size_t p=0;p<indices.size();++p){uint32_t value{};memcpy(&value,canvas.data()+p*4,4);equal&=value==colors[indices[p]];}
            Expect(equal,"composited text, distant changes and cursor erase are exact");expected.push_back(canvas);delays.push_back(delay);
            Png(folder/(L"frame-"+std::to_wstring(i)+L".png"),canvas,w,h);
        });
        Expect(info.count==3&&info.duration==50&&delays[0]==30,"duplicates merge without timing changes");
        Expect(info.transparent&&info.cropped,"transparent reuse and cropped deltas are present");
        const auto longFile=folder/L"long.gif";
        {GifFrames frames(f.Get(),longFile,w,h,colors,false,{});frames.Add(a,131080);frames.Add(b,7);frames.Finish();}
        const auto longInfo=Decode(longFile,[&](const auto& canvas,unsigned,UINT i){Expect(canvas==expected[i<3?0:1],"long delay split preserves the canvas");});
        Expect(longInfo.duration==131087&&longInfo.count==4,"delay overflow splits at GIF limit");
        OptimizeGif(longFile,false,{},[](int){});Expect(Decode(longFile).duration==131087,"non-looping optimization preserves long duration");
        std::vector<BYTE> rgb(w*h*4),mapped(w*h);for(size_t i=0;i<a.size();++i)memcpy(rgb.data()+i*4,&colors[a[i]],4);
        GifQuantizer q(colors);q.Map(rgb.data(),w,h,mapped.data());Expect(mapped==a,"exact palette colors bypass dithering");
        auto before=mapped;rgb[0]=123;rgb[1]=65;rgb[2]=43;q.Map(rgb.data(),w,h,mapped.data());
        Expect(std::equal(mapped.begin()+1,mapped.end(),before.begin()+1),"one pixel change cannot disturb static pixels");
        // Compare the accelerated mapping with a full precision exhaustive search.
        for(size_t i=0;i<size_t(w)*h;++i){rgb[i*4]=BYTE(i*37);rgb[i*4+1]=BYTE(i*73+i/256);rgb[i*4+2]=BYTE(i*19+i/17);}
        q.Map(rgb.data(),w,h,mapped.data());bool nearestExact=true;
        for(size_t i=0;i<mapped.size();++i){int best=0,distance=INT_MAX;
            for(int j=0;j<int(colors.size());++j){const int dr=int(rgb[i*4+2])-int((colors[j]>>16)&255),dg=int(rgb[i*4+1])-int((colors[j]>>8)&255),db=int(rgb[i*4])-int(colors[j]&255);const int error=2*dr*dr+4*dg*dg+db*db;if(error<distance){distance=error;best=j;}}
            nearestExact&=mapped[i]==best;
        }
        Expect(nearestExact,"quantization matches exhaustive full precision nearest-color search");
        for(size_t i=0;i<mapped.size();++i){rgb[i*4]=103;rgb[i*4+1]=103;rgb[i*4+2]=103;}
        const std::array<uint32_t,2> closeColors{0xff646464,0xff6c6c6c};GifQuantizer flat(closeColors);flat.Map(rgb.data(),w,h,mapped.data());
        Expect(std::all_of(mapped.begin(),mapped.end(),[](BYTE v){return v==0;}),"flat colors do not acquire Bayer speckles or 5-bit rounding errors");
        std::vector<BYTE> ramp(255*4);for(int i=0;i<255;++i){ramp[i*4]=BYTE(i);ramp[i*4+1]=BYTE(i);ramp[i*4+2]=BYTE(i);ramp[i*4+3]=255;}
        const auto precisePalette=GifPalette(ramp);GifQuantizer precise(precisePalette);std::vector<BYTE> rampIndices(255);precise.Map(ramp.data(),255,1,rampIndices.data());bool rampExact=true;
        for(int i=0;i<255;++i)rampExact&=(precisePalette[rampIndices[i]]&255)==uint32_t(i);
        Expect(rampExact&&precisePalette.size()==255,"palette preserves all 255 subtle grayscale levels without quantization loss");
        wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);const auto bin=std::filesystem::path(module).parent_path();
        const auto real=bin/L"gifsicle.exe";
        if(!std::filesystem::exists(real))throw std::runtime_error("Real gifsicle is required for this test");
        const auto lossless=folder/L"lossless.gif";
        std::wstring command=L"\""+real.native()+L"\" -O3 \""+raw.native()+L"\" -o \""+lossless.native()+L"\"";
        STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
        Check(CreateProcessW(real.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)?S_OK:E_FAIL,"Test gifsicle");
        WaitForSingleObject(pi.hProcess,30000);DWORD code{};GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);Expect(code==0,"real lossless optimizer completes");
        std::vector<std::vector<BYTE>> actual;
        const auto optimizedInfo=Decode(lossless,[&](const auto& canvas,unsigned delay,UINT){for(unsigned t=0;t<delay;++t)actual.push_back(canvas);});
        size_t t=0;bool equal=true;for(size_t i=0;i<expected.size();++i)for(unsigned j=0;j<delays[i];++j,++t)equal&=t<actual.size()&&actual[t]==expected[i];
        Expect(equal&&optimizedInfo.duration==info.duration,"lossless optimization matches every displayed pixel and delay");
        const std::array<uint32_t,6> nearColors{0xff646464,0xff656565,0xff666666,0xff676767,0xff000000,0xffffffff};
        std::vector<BYTE> noise(w*h);for(size_t i=0;i<noise.size();++i)noise[i]=BYTE(((i*73856093u)^(i/size_t(w)*19349663u))%4);
        for(int y=10;y<40;++y)for(int x=10;x<70;++x)if(x%7==0||y%9==0)noise[size_t(y)*w+x]=4;
        const auto noisy=folder/L"near-colors.gif",lossy=folder/L"lossy10.gif";
        {GifFrames frames(f.Get(),noisy,w,h,nearColors,true,{});frames.Add(noise,10);frames.Finish();}
        command=L"\""+real.native()+L"\" -O3 --lossy=10 \""+noisy.native()+L"\" -o \""+lossy.native()+L"\"";pi={};
        Check(CreateProcessW(real.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)?S_OK:E_FAIL,"Test lossy gifsicle");
        WaitForSingleObject(pi.hProcess,30000);GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);Expect(code==0,"real lossy=10 optimizer completes");
        double lossyError=0;size_t altered=0;bool textExact=true;
        Decode(lossy,[&](const auto& image,unsigned,UINT){for(size_t i=0;i<noise.size();++i){uint32_t value{};memcpy(&value,image.data()+i*4,4);const auto reference=nearColors[noise[i]];
            altered+=value!=reference;if(noise[i]==4)textExact&=value==reference;
            for(int channel=0;channel<3;++channel){const int diff=int((value>>(channel*8))&255)-int((reference>>(channel*8))&255);lossyError+=diff*diff;}}
            Png(folder/L"lossy10.png",image,w,h);
        });
        Expect(altered>0&&textExact&&lossyError/(w*h*3)<25,"actual lossy changes retain text edges and bounded color error");
        const auto clean=folder/L"quality-first.gif";std::filesystem::copy_file(noisy,clean,std::filesystem::copy_options::overwrite_existing);
        OptimizeGif(clean,true,{},[](int){});bool preserved=true;
        Decode(clean,[&](const auto& image,unsigned,UINT){for(size_t i=0;i<noise.size();++i){uint32_t value{};memcpy(&value,image.data()+i*4,4);preserved&=value==nearColors[noise[i]];}});
        Expect(preserved,"production optimizer never damages near-color details to save bytes");
        // Slow changes must eventually update: compare against the retained
        // canvas, not each new source frame, so error can never accumulate.
        const std::array<uint32_t,6> fadeColors{0xff404040,0xff444444,0xff484848,0xff4c4c4c,0xff000000,0xffffffff};
        const auto temporal=folder/L"temporal.gif";std::vector<BYTE> fade(w*h,0);
        {GifFrames frames(f.Get(),temporal,w,h,fadeColors,true,{},8);
            for(BYTE i=0;i<4;++i){std::fill(fade.begin(),fade.end(),i);fade[10*w+10]=i<2?4:5;frames.Add(fade,10);}frames.Finish();}
        unsigned elapsed=0;bool bounded=true,edges=true;
        const auto temporalInfo=Decode(temporal,[&](const auto& canvas,unsigned delay,UINT){
            for(unsigned tick=elapsed;tick<elapsed+delay;++tick){const auto sourceColor=fadeColors[tick/10];
                for(int channel=0;channel<3;++channel)bounded&=std::abs(int(canvas[channel])-int((sourceColor>>(channel*8))&255))<=8;
                edges&=canvas[(10*w+10)*4]==(tick<20?0:255);
            }elapsed+=delay;
        });
        Expect(bounded&&elapsed==40&&temporalInfo.count<4,"temporal reuse bounds error through slow fades and merges redundant frames");
        Expect(edges,"high contrast text changes remain immediate");
        std::vector<BYTE> finalCanvas;Decode(temporal,[&](const auto& canvas,unsigned,UINT){finalCanvas=canvas;});
        Expect(finalCanvas[0]==76,"slow fade refreshes rather than accumulating stale pixels");
        const auto optimized=folder/L"optimized.gif";std::filesystem::copy_file(raw,optimized,std::filesystem::copy_options::overwrite_existing);
        OptimizeGif(optimized,true,{},[](int){});const auto oi=Decode(optimized);
        Expect(oi.duration==info.duration&&oi.width==w&&oi.height==h&&std::filesystem::file_size(optimized)<=std::filesystem::file_size(raw),"production optimization keeps valid smallest output");
        // Padding creates a valid larger original for small-but-invalid candidate tests.
        auto padded=Bytes(raw);padded.pop_back();padded.push_back(0x21);padded.push_back(0xfe);
        for(int i=0;i<16;++i){padded.push_back(255);padded.insert(padded.end(),255,'p');}padded.push_back(0);padded.push_back(0x3b);
        const auto input=folder/L"padded.gif";
        ExportDirectory child(folder);const auto fake=child.Path()/L"fixture.exe";
        std::filesystem::copy_file(bin/L"lumashot_gif_optimizer_fixture.exe",fake);
        for(const char* mode:{"failure","invalid","dimensions","duration","loop","larger","timeout","memory"}){
            std::ofstream(child.Path()/L"mode.txt")<<mode;Write(input,padded);
            detail::OptimizeGifWithTool(input,true,{},[](int){},{fake,100,256ull*1024*1024});
            Expect(Bytes(input)==padded,(std::string("fallback preserves original: ")+mode).c_str());
        }
        Write(input,padded);detail::OptimizeGifWithTool(input,true,{},[](int){},{child.Path()/L"missing.exe"});Expect(Bytes(input)==padded,"missing tool preserves original");
        std::ofstream(child.Path()/L"mode.txt")<<"valid";std::ofstream(child.Path()/L"inputs.txt",std::ios::trunc).close();
        detail::OptimizeGifWithTool(input,true,{},[](int){},{fake});Expect(Bytes(input)==Bytes(raw),"smaller valid candidate is accepted");
        {std::ifstream inputs(child.Path()/L"inputs.txt");uint64_t first{},second{};inputs>>first;const bool extra=bool(inputs>>second);Expect(first==padded.size()&&!extra,"only one lossless candidate is generated from the original GIF");}
        Write(input,padded);std::ofstream(child.Path()/L"mode.txt")<<"timeout";std::stop_source cancel;
        std::jthread trigger([&]{std::this_thread::sleep_for(std::chrono::milliseconds(100));cancel.request_stop();});bool stopped=false;
        try{detail::OptimizeGifWithTool(input,true,cancel.get_token(),[](int){},{fake});}catch(const std::exception&){stopped=true;}
        Expect(stopped&&Bytes(input)==padded,"cancel terminates optimizer without replacing original");
        const auto destination=folder/L"existing.gif";Write(destination,{1,2,3});
        try{SaveOutput(input,destination,cancel.get_token());}catch(const std::exception&){}
        Expect(Bytes(destination)==std::vector<BYTE>({1,2,3}),"canceled save leaves existing destination intact");
        size_t temporaryCount=0;for(const auto& entry:std::filesystem::directory_iterator(folder))if(entry.is_directory()&&entry.path().filename().native().starts_with(L"gif-export-"))++temporaryCount;
        Expect(temporaryCount==1,"all optimizer temporary directories cleaned (only fixture remains)");
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;++failures;}
    CoUninitialize();return failures?1:0;
}
