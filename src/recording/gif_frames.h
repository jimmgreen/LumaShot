#pragma once
#include "recording/encoder.h"
#include <wincodec.h>
#include <span>
#include <vector>

namespace lumashot::recording {
// The input palette contains opaque colors only. Index 255 is reserved for
// reuse of the previous canvas and never appears in quantized input pixels.
class GifFrames {
    ComPtr<IWICStream> stream_;
    ComPtr<IWICBitmapEncoder> encoder_;
    int width_,height_;
    std::stop_token stop_;
    std::vector<BYTE> pending_,previous_;
    unsigned delay_{};
    bool reuseSimilar_{};
    std::array<BYTE,256*256> reusable_{};
    std::vector<BYTE> normalized_;

    void Flush();
public:
    GifFrames(IWICImagingFactory* factory,const std::filesystem::path& output,
        int width,int height,std::span<const uint32_t> colors,bool loop,std::stop_token stop,unsigned colorTolerance=0);
    void Add(std::span<const BYTE> pixels,uint64_t centiseconds);
    void Finish();
};
}
