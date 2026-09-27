#include "recording/mp4_export.h"
#include "recording/encoder.h"
#include "recording/panel.h"
#include "recording/storage.h"
#include "gif_test_helpers.h"
#include <d3d10.h>
#include <iostream>
#include <thread>
using namespace lumashot::recording;
namespace {int failures=0;void Expect(bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<std::endl;failures+=!ok;}}
int main(){CoInitializeEx(nullptr,COINIT_MULTITHREADED);MFStartup(MF_VERSION);
 try{
    const auto folder=std::filesystem::current_path()/L"mp4-export-fixture";std::filesystem::create_directories(folder);
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_VIDEO_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"Fixture GPU");ComPtr<ID3D10Multithread> guard;context.As(&guard);guard->SetMultithreadProtected(TRUE);
    const auto make=[&](const std::filesystem::path& path,int w,bool audio,bool irregular=false){constexpr int h=180;std::vector<uint32_t> pixels(size_t(w)*h);std::vector<short> sound(1600*2,1200);
        D3D11_TEXTURE2D_DESC d{};d.Width=UINT(w);d.Height=h;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.BindFlags=D3D11_BIND_RENDER_TARGET;ComPtr<ID3D11Texture2D> texture;Check(device->CreateTexture2D(&d,nullptr,&texture),"Test texture");
        Encoder encoder(device.Get(),path,{w,h},{w,h},30,true,audio);
        for(int i=0;i<600;++i){for(int y=0;y<h;++y)for(int x=0;x<w;++x){const uint32_t v=x==((i/30)%w)||x%16==0||y%24==0?32:220;pixels[size_t(y)*w+x]=0xff000000|v*0x010101;}context->UpdateSubresource(texture.Get(),0,nullptr,pixels.data(),UINT(w)*4,0);encoder.Frame(texture.Get(),{0,0,w,h},i*10000000LL/30+(irregular&&i>=300?500000:0)+(irregular&&i%7==0?8000:0),10000000LL/30);if(audio)encoder.AudioFrame(sound.data(),1600,int64_t(i)*1600);}encoder.Finish();
    };
    const auto raw=folder/L"raw.mp4",source=folder/L"padded.mp4",destination=folder/L"saved.mp4";make(raw,320,true);
    auto padded=gif_test::Bytes(raw);padded.insert(padded.end(),{0,16,0,0,'f','r','e','e'});padded.resize(padded.size()+1048576-8);gif_test::Write(source,padded);
    int last=-1;bool monotonic=true,committed=false;std::vector<Mp4StageProgress> phases;
    const auto exported=ExportMp4ToFile(source,destination,{},[&](int value){monotonic&=value>=last;last=value;if(value==100)committed=std::filesystem::is_regular_file(destination);},[&](const Mp4StageProgress& p){phases.push_back(p);});
    Expect(monotonic&&last==100&&committed,"progress completes only after committed MP4");
    bool encoded=false,verified=false,phase_monotonic=true;
    for(size_t i=0;i<phases.size();++i){const auto& p=phases[i];encoded|=p.stage==Mp4Stage::Encode&&p.percent==100;verified|=p.stage==Mp4Stage::Verify&&p.percent==100;
        phase_monotonic&=p.percent>=-1&&p.percent<=100;if(i&&p.stage==phases[i-1].stage&&p.attempt==phases[i-1].attempt)phase_monotonic&=p.percent>=phases[i-1].percent;}
    Expect(encoded&&verified&&phase_monotonic&&phases.front().stage==Mp4Stage::Inspect&&phases.back().stage==Mp4Stage::Complete,"named phases report real local completion and bounded monotonic progress");
    Expect(std::filesystem::file_size(destination)<padded.size(),"real AV1 candidate passes timing audio and quality checks and is adopted");
    Expect(exported.encoding==Mp4Encoding::Av1&&exported.saved_bytes==std::filesystem::file_size(destination),"default export reports adopted AV1 and actual file size");
    wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);const auto bin=std::filesystem::path(module).parent_path();
    const auto compatible=folder/L"compatible.mp4";
    const auto h264=detail::ExportMp4WithTool(source,compatible,{},[](int){},{bin/L"ffmpeg.exe",900000,3ull*1024*1024*1024,false});
    Expect(h264.encoding==Mp4Encoding::H264,"compatible H.264 path remains available");
    const auto preview=ReadPreview(compatible,200000000,{});Expect(!preview.poster.pixels.empty()&&preview.thumbnails.size()==8,"long GOP preview returns poster and all eight thumbnails");
    std::stop_source already;already.request_stop();const auto canceled=ReadPreview(compatible,200000000,already.get_token());Expect(canceled.poster.pixels.empty()&&canceled.thumbnails.empty(),"preview respects pre-cancellation");
    const auto vfr=folder/L"vfr.mp4",vfrSaved=folder/L"vfr-saved.mp4";make(vfr,320,true,true);
    auto vfrBytes=gif_test::Bytes(vfr);vfrBytes.insert(vfrBytes.end(),{0,16,0,0,'f','r','e','e'});vfrBytes.resize(vfrBytes.size()+1048576-8);gif_test::Write(vfr,vfrBytes);
    const auto variable=ExportMp4ToFile(vfr,vfrSaved,{},[](int){});
    Expect(variable.encoding==Mp4Encoding::Av1&&variable.saved_bytes<variable.original_bytes,"VFR jitter and a capture gap preserve every frame timestamp and compressed audio without silent fallback");
    ExportDirectory fixture(folder);const auto fake=fixture.Path()/L"fixture.exe";
    std::filesystem::copy_file(bin/L"lumashot_mp4_optimizer_fixture.exe",fake);std::filesystem::copy_file(raw,fixture.Path()/L"candidate.mp4");make(fixture.Path()/L"alternate.mp4",160,false);
    for(const char* mode:{"failure","invalid","larger","mismatch","quality","meanquality","tailquality","shortquality","timeout","memory"}){
        std::ofstream(fixture.Path()/L"mode.txt")<<mode;detail::ExportMp4WithTool(source,destination,{},[](int){},{fake,100,64ull*1024*1024});
        Expect(gif_test::Bytes(destination)==padded,(std::string("safe fallback: ")+mode).c_str());
    }
    const auto missing=detail::ExportMp4WithTool(source,destination,{},[](int){},{folder/L"missing.exe"});Expect(missing.encoding==Mp4Encoding::Original,"fallback is explicitly reported to caller");Expect(gif_test::Bytes(destination)==padded,"missing optimizer preserves original");
    std::ofstream(fixture.Path()/L"mode.txt")<<"valid";detail::ExportMp4WithTool(source,destination,{},[](int){},{fake});Expect(gif_test::Bytes(destination)==gif_test::Bytes(raw),"valid smaller candidate is saved");
    std::ofstream(fixture.Path()/L"mode.txt")<<"progress";bool halfway=false;
    detail::ExportMp4WithTool(source,destination,{},[](int){},{fake},[&](const Mp4StageProgress& p){halfway|=p.stage==Mp4Stage::Verify&&p.percent==50;});
    Expect(halfway,"SSIM progress uses completed frames rather than synthetic setpts timestamps");
    std::ofstream(fixture.Path()/L"mode.txt")<<"rarequality";detail::ExportMp4WithTool(source,destination,{},[](int){},{fake});Expect(gif_test::Bytes(destination)==gif_test::Bytes(raw),"isolated bounded quality dips do not force whole-clip size inflation");
    gif_test::Write(destination,{1,2,3});std::ofstream(fixture.Path()/L"mode.txt")<<"timeout";std::stop_source cancel;std::jthread trigger([&]{std::this_thread::sleep_for(std::chrono::milliseconds(100));cancel.request_stop();});bool stopped=false;
    try{detail::ExportMp4WithTool(source,destination,cancel.get_token(),[](int){},{fake});}catch(...){stopped=true;}
    Expect(stopped&&gif_test::Bytes(destination)==std::vector<BYTE>({1,2,3}),"cancel terminates process and does not replace destination");
    std::stop_source atEncode;stopped=false;
    try{detail::ExportMp4WithTool(source,destination,atEncode.get_token(),[](int){},{fake},[&](const Mp4StageProgress& p){if(p.stage==Mp4Stage::Encode&&p.percent==0)atEncode.request_stop();});}catch(...){stopped=true;}
    Expect(stopped&&gif_test::Bytes(destination)==std::vector<BYTE>({1,2,3}),"cancel at encoding start joins source inspection and preserves destination");
    std::ofstream(fixture.Path()/L"mode.txt")<<"valid";
    const auto interrupted=detail::ExportMp4WithTool(source,destination,{},[](int){},{fake},[](const Mp4StageProgress& p){if(p.stage==Mp4Stage::Encode)throw std::runtime_error("Synthetic progress failure");});
    Expect(interrupted.encoding==Mp4Encoding::Original&&gif_test::Bytes(destination)==padded,"encoding callback failure joins source inspection before safe fallback");
    gif_test::Write(destination,{1,2,3});
    std::ofstream(fixture.Path()/L"mode.txt")<<"valid";std::stop_source beforeSave;stopped=false;
    try{detail::ExportMp4WithTool(source,destination,beforeSave.get_token(),[&](int p){if(p==99)beforeSave.request_stop();},{fake});}catch(...){stopped=true;}
    Expect(stopped&&gif_test::Bytes(destination)==std::vector<BYTE>({1,2,3}),"cancel before commit preserves previous file");
    size_t dirs=0;for(const auto& e:std::filesystem::directory_iterator(folder))if(e.is_directory()&&e.path().filename().native().starts_with(L"gif-export-"))++dirs;
    Expect(dirs==1,"optimizer cleans temporary directories");
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;++failures;}
 MFShutdown();CoUninitialize();return failures?1:0;
}
