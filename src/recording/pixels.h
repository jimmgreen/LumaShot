#pragma once
#include "recording/encoder.h"
#include <vector>
#include <mferror.h>
namespace lumashot::recording {
// Storage dimensions/pitch and the visible image are separate: H.264 decoders
// can renegotiate padded storage on the first ReadSample or after a seek.
inline void CopyRgb32Region(IMFSample* sample,UINT width,UINT height,LONG fallbackStride,
    const RECT& visible,std::vector<BYTE>& pixels){
    if(!width||!height||width>32768||height>32768||visible.left<0||visible.top<0||
        visible.right<=visible.left||visible.bottom<=visible.top||visible.right>LONG(width)||visible.bottom>LONG(height))
        throw std::runtime_error("Invalid decoded dimensions or aperture");
    const auto rowBytes=size_t(visible.right-visible.left)*4;
    pixels.resize(rowBytes*size_t(visible.bottom-visible.top));
    const auto copy=[&](const BYTE* top,LONG pitch){
        if(std::abs(static_cast<long long>(pitch))<width*4LL)throw std::runtime_error("Invalid decoded surface pitch");
        for(LONG y=visible.top;y<visible.bottom;++y)
            memcpy(pixels.data()+size_t(y-visible.top)*rowBytes,top+ptrdiff_t(y)*pitch+size_t(visible.left)*4,rowBytes);
    };
    DWORD count{};Check(sample->GetBufferCount(&count),"Decoded buffer count");ComPtr<IMFMediaBuffer> buffer;
    if(count==1)Check(sample->GetBufferByIndex(0,&buffer),"Decoded surface");
    else Check(sample->ConvertToContiguousBuffer(&buffer),"Decoded contiguous buffer");
    ComPtr<IMF2DBuffer> two;
    if(SUCCEEDED(buffer.As(&two))){
        BYTE* top{};LONG pitch{};Check(two->Lock2D(&top,&pitch),"Decoded surface pitch");
        struct Unlock{IMF2DBuffer* b;~Unlock(){b->Unlock2D();}} unlock{two.Get()};copy(top,pitch);return;
    }
    BYTE* data{};DWORD length{};Check(buffer->Lock(&data,nullptr,&length),"Decoded linear pixels");
    struct Unlock{IMFMediaBuffer* b;~Unlock(){b->Unlock();}} unlock{buffer.Get()};
    const auto pitch=std::abs(static_cast<long long>(fallbackStride));
    if(pitch<width*4LL||size_t(pitch)*(height-1)+size_t(width)*4>length)throw std::runtime_error("Invalid decoded linear pitch");
    copy(data+(fallbackStride<0?size_t(pitch)*(height-1):0),fallbackStride);
}
inline void CopyRgb32(IMFSample* sample,UINT width,UINT height,LONG stride,std::vector<BYTE>& pixels){
    CopyRgb32Region(sample,width,height,stride,{0,0,LONG(width),LONG(height)},pixels);
}
inline void CopyDecodedRgb32(IMFSourceReader* reader,IMFSample* sample,UINT visibleWidth,UINT visibleHeight,std::vector<BYTE>& pixels){
    // Query AFTER ReadSample. Caching this before the first sample misses
    // MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED, including after seeking.
    ComPtr<IMFMediaType> type;Check(reader->GetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM),&type),"Current decoded layout");
    GUID subtype{};Check(type->GetGUID(MF_MT_SUBTYPE,&subtype),"Decoded pixel format");
    if(subtype!=MFVideoFormat_RGB32)throw std::runtime_error("Decoded pixel format changed");
    UINT width{},height{},rawStride{};Check(MFGetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,&width,&height),"Decoded surface size");
    if(!width||!height||width>32768||height>32768)throw std::runtime_error("Invalid decoded surface dimensions");
    if(FAILED(type->GetUINT32(MF_MT_DEFAULT_STRIDE,&rawStride)))rawStride=width*4;
    RECT visible{0,0,LONG(visibleWidth),LONG(visibleHeight)};
    MFVideoArea area{};UINT bytes{};
    HRESULT aperture=type->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE,reinterpret_cast<BYTE*>(&area),sizeof(area),&bytes);
    if(aperture==MF_E_ATTRIBUTENOTFOUND)aperture=type->GetBlob(MF_MT_GEOMETRIC_APERTURE,reinterpret_cast<BYTE*>(&area),sizeof(area),&bytes);
    if(SUCCEEDED(aperture)){
        if(bytes!=sizeof(area)||area.OffsetX.fract||area.OffsetY.fract||
            area.Area.cx!=LONG(visibleWidth)||area.Area.cy!=LONG(visibleHeight))
            throw std::runtime_error("Decoded visible dimensions changed");
        visible={area.OffsetX.value,area.OffsetY.value,area.OffsetX.value+area.Area.cx,area.OffsetY.value+area.Area.cy};
    }else if(aperture!=MF_E_ATTRIBUTENOTFOUND)Check(aperture,"Decoded display aperture");
    CopyRgb32Region(sample,width,height,LONG(rawStride),visible,pixels);
}
}
