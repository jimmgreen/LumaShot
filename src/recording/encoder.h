#pragma once
#include "recording/core.h"
#include <d3d11.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <array>
namespace lumashot::recording {
using Microsoft::WRL::ComPtr;
struct TexturePool;
void Check(HRESULT result,const char* action);
class Encoder {
    ComPtr<IMFSinkWriter> writer_;ComPtr<IMFDXGIDeviceManager> manager_;
    ComPtr<ID3D11Device> device_;ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11VideoDevice> video_;ComPtr<ID3D11VideoContext> video_context_;
    ComPtr<ID3D11VideoProcessorEnumerator> enumerator_;ComPtr<ID3D11VideoProcessor> processor_;
    ComPtr<ID3D11Texture2D> input_source_;
    ComPtr<ID3D11VideoProcessorInputView> input_view_;
    ComPtr<ID3D11VideoProcessorOutputView> output_view_;
    ComPtr<ID3D11Texture2D> output_;DWORD stream_{};SIZE size_{};bool software_{};
    ComPtr<ID3D11Texture2D> staging_;DWORD audio_stream_{};bool audio_{};bool quality_control_{};
    std::shared_ptr<TexturePool> pool_;
public:
    Encoder(ID3D11Device* device,const std::filesystem::path& file,SIZE input,SIZE output,int fps,bool software,bool audio=false,int quality_preset=1);
    void Frame(ID3D11Texture2D* source,RECT crop,long long time,long long duration);
    void Finish();
    bool UsesQualityControl()const noexcept{return quality_control_;}
    void AudioFrame(const short* data,size_t frames,long long first_frame);
};
}
