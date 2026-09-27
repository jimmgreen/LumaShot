#pragma once
#include <windows.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
namespace lumashot::recording {
enum class State {Ready,Starting,Recording,Paused,Finishing,Preview,Failed};
struct Options {
    RECT region{};HMONITOR monitor{};HWND target{};
    int fps{30},width{0},quality{1};bool cursor{true},software{},system_audio{true},microphone{};
    bool synthetic{};int synthetic_frames{60};
    SIZE synthetic_size{640,360};
    long long duration_limit{};
};
struct Status {State state{State::Ready};long long time{};unsigned frames{},dropped{};std::wstring detail;};
inline SIZE OutputSize(int w,int h,int limit){
    if(w<=0||h<=0||w>32768||h>32768)throw std::runtime_error("Invalid recording size");
    const double scale=limit>0?std::min(1.,double(limit)/w):1.;
    return {std::max(2L,(LONG(w*scale)+1)&~1L),std::max(2L,(LONG(h*scale)+1)&~1L)};
}
class Clock {
    using Time=std::chrono::steady_clock;
    Time::time_point start_{Time::now()},pause_{};Time::duration paused_{};bool stopped_{};
public:
    void Pause(bool value){if(value==stopped_)return;if(value)pause_=Time::now();else paused_+=Time::now()-pause_;stopped_=value;}
    long long Now()const{return std::chrono::duration_cast<std::chrono::nanoseconds>((stopped_?pause_:Time::now())-start_-paused_).count()/100;}
};
class Session {
    std::jthread thread_;std::atomic<bool> pause_{},stop_{};
public:
    ~Session(){Stop();if(thread_.joinable())thread_.join();}
    void Start(Options options,std::filesystem::path file,std::function<void(Status)> report);
    void Pause(bool value){pause_=value;}
    void Stop(){stop_=true;}
};
struct GifOptions {int fps{15},width{0};long long begin{},end{};bool loop{true};};
void ExportGif(const std::filesystem::path& source,const std::filesystem::path& output,const GifOptions& options,std::stop_token stop,const std::function<void(int)>& progress);
}




