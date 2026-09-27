#pragma once
#include "recording/encoder.h"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <deque>
#include <vector>
namespace lumashot::recording {
class Audio {
    struct Source {ComPtr<IAudioClient> client;ComPtr<IAudioCaptureClient> capture;std::deque<short> samples;};
    std::vector<Source> sources_;
public:
    Audio(bool system,bool microphone){
        ComPtr<IMMDeviceEnumerator> enumerator;Check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator)),"Audio devices");
        for(int i=0;i<2;++i){if(!(i?microphone:system))continue;Source source;ComPtr<IMMDevice> device;Check(enumerator->GetDefaultAudioEndpoint(i?eCapture:eRender,eConsole,&device),"Default audio device");Check(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,&source.client),"Audio capture client");
            WAVEFORMATEX format{WAVE_FORMAT_PCM,2,48000,192000,4,16,0};const DWORD flags=AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY|(i?0:AUDCLNT_STREAMFLAGS_LOOPBACK);
            Check(source.client->Initialize(AUDCLNT_SHAREMODE_SHARED,flags,1000000,0,&format,nullptr),"Audio 48kHz stereo capture");Check(source.client->GetService(IID_PPV_ARGS(&source.capture)),"Audio packets");sources_.push_back(std::move(source));
        }
    }
    ~Audio(){for(auto& source:sources_)source.client->Stop();}
    void Pause(bool paused){for(auto& source:sources_){if(paused){source.client->Stop();source.client->Reset();source.samples.clear();}else Check(source.client->Start(),"Start audio");}}
    std::vector<short> Read(size_t frames){
        std::vector<int> mix(frames*2);
        for(auto& source:sources_){UINT packet{};Check(source.capture->GetNextPacketSize(&packet),"Audio packet size");while(packet){BYTE* data{};UINT count{};DWORD flags{};Check(source.capture->GetBuffer(&data,&count,&flags,nullptr,nullptr),"Audio packet");
                if(source.samples.size()+size_t(count)*2>48000){source.capture->ReleaseBuffer(count);throw std::runtime_error("Audio buffer exceeded 500 ms; recording stopped to prevent audio drift.");}
                for(UINT i=0;i<count*2;++i)source.samples.push_back(flags&AUDCLNT_BUFFERFLAGS_SILENT?0:reinterpret_cast<short*>(data)[i]);source.capture->ReleaseBuffer(count);Check(source.capture->GetNextPacketSize(&packet),"Next audio packet");}
            for(size_t i=0;i<mix.size()&&!source.samples.empty();++i){mix[i]+=source.samples.front();source.samples.pop_front();}
        }
        std::vector<short> result(mix.size());for(size_t i=0;i<mix.size();++i)result[i]=static_cast<short>(std::clamp(mix[i],-32768,32767));return result;
    }
};
}
