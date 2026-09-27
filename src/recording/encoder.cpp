#include "recording/encoder.h"
#include "recording/quality.h"
#include <mferror.h>
#include <codecapi.h>
#include <sstream>
#include <evr.h>
#include <condition_variable>
#include <mutex>
namespace lumashot::recording {
struct TexturePool {
    std::array<ComPtr<ID3D11Texture2D>,8> textures;std::array<bool,8> busy{};std::mutex mutex;std::condition_variable released;
    size_t Acquire(){std::unique_lock lock(mutex);if(!released.wait_for(lock,std::chrono::seconds(3),[&]{return std::find(busy.begin(),busy.end(),false)!=busy.end();}))throw std::runtime_error("Encoder stalled: GPU frame queue reached its limit");const auto slot=size_t(std::find(busy.begin(),busy.end(),false)-busy.begin());busy[slot]=true;return slot;}
    void Release(size_t slot){{std::lock_guard lock(mutex);busy[slot]=false;}released.notify_one();}
};
class Recycle final:public IMFAsyncCallback {
    std::atomic<ULONG> refs_{1};std::shared_ptr<TexturePool> pool_;size_t slot_;
public:
    Recycle(std::shared_ptr<TexturePool> pool,size_t slot):pool_(std::move(pool)),slot_(slot){}
    STDMETHODIMP QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id==__uuidof(IUnknown)||id==__uuidof(IMFAsyncCallback)){*out=static_cast<IMFAsyncCallback*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    STDMETHODIMP_(ULONG) AddRef()override{return ++refs_;}
    STDMETHODIMP_(ULONG) Release()override{const auto value=--refs_;if(!value)delete this;return value;}
    STDMETHODIMP GetParameters(DWORD*,DWORD*)override{return E_NOTIMPL;}
    STDMETHODIMP Invoke(IMFAsyncResult*)override{pool_->Release(slot_);return S_OK;}
};
void Check(HRESULT hr,const char* action){if(FAILED(hr)){std::ostringstream s;s<<action<<" (0x"<<std::hex<<static_cast<unsigned long>(hr)<<")";throw std::runtime_error(s.str());}}
Encoder::Encoder(ID3D11Device* device,const std::filesystem::path& file,SIZE input,SIZE output,int fps,bool software,bool audio,int quality_preset):device_(device),size_(output),software_(software),audio_(audio){
    device_->GetImmediateContext(&context_);Check(device_.As(&video_),"D3D11 video device");Check(context_.As(&video_context_),"D3D11 video context");
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc{};desc.InputFrameFormat=D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;desc.InputWidth=input.cx;desc.InputHeight=input.cy;desc.OutputWidth=output.cx;desc.OutputHeight=output.cy;desc.Usage=D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;desc.InputFrameRate={UINT(fps),1};desc.OutputFrameRate=desc.InputFrameRate;
    Check(video_->CreateVideoProcessorEnumerator(&desc,&enumerator_),"Video processor formats");Check(video_->CreateVideoProcessor(enumerator_.Get(),0,&processor_),"GPU color conversion");
    D3D11_TEXTURE2D_DESC texture{};texture.Width=output.cx;texture.Height=output.cy;texture.MipLevels=texture.ArraySize=1;texture.Format=DXGI_FORMAT_NV12;texture.SampleDesc.Count=1;texture.BindFlags=D3D11_BIND_RENDER_TARGET;
    Check(device_->CreateTexture2D(&texture,nullptr,&output_),"NV12 output texture");
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC out{};out.ViewDimension=D3D11_VPOV_DIMENSION_TEXTURE2D;
    Check(video_->CreateVideoProcessorOutputView(output_.Get(),enumerator_.Get(),&out,&output_view_),"GPU output view");
    RECT destination{0,0,size_.cx,size_.cy};
    video_context_->VideoProcessorSetStreamDestRect(processor_.Get(),0,TRUE,&destination);video_context_->VideoProcessorSetOutputTargetRect(processor_.Get(),TRUE,&destination);
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE rgb{};rgb.RGB_Range=0;D3D11_VIDEO_PROCESSOR_COLOR_SPACE yuv{};yuv.YCbCr_Matrix=1;yuv.Nominal_Range=1;
    video_context_->VideoProcessorSetStreamColorSpace(processor_.Get(),0,&rgb);video_context_->VideoProcessorSetOutputColorSpace(processor_.Get(),&yuv);
    if(!software){pool_=std::make_shared<TexturePool>();for(auto& surface:pool_->textures)Check(device_->CreateTexture2D(&texture,nullptr,&surface),"Bounded encoder texture pool");}
    ComPtr<IMFAttributes> attributes;Check(MFCreateAttributes(&attributes,5),"Encoder attributes");
    if(!software){UINT token{};Check(MFCreateDXGIDeviceManager(&token,&manager_),"DXGI device manager");Check(manager_->ResetDevice(device,token),"Encoder GPU device");Check(attributes->SetUnknown(MF_SINK_WRITER_D3D_MANAGER,manager_.Get()),"GPU encoder manager");}
    Check(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,software?FALSE:TRUE),"Hardware encoding preference");
    // Synchronous sink writer throttling provides backpressure. Input samples own
    // independent surfaces, and WriteSample is never called from the UI thread.
    Check(attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING,FALSE),"Encoder backpressure");
    Check(MFCreateSinkWriterFromURL(file.c_str(),nullptr,attributes.Get(),&writer_),"Create MP4");
    ComPtr<IMFMediaType> type;Check(MFCreateMediaType(&type),"H264 media type");
    type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_H264);type->SetUINT32(MF_MT_MPEG2_PROFILE,eAVEncH264VProfile_High);type->SetUINT32(MF_MT_AVG_BITRATE,VideoBitrate(output.cx,output.cy,fps,quality_preset));type->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);
    MFSetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,output.cx,output.cy);MFSetAttributeRatio(type.Get(),MF_MT_FRAME_RATE,fps,1);MFSetAttributeRatio(type.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1);
    Check(writer_->AddStream(type.Get(),&stream_),"H264 output stream");
    ComPtr<IMFMediaType> outputType=type;
    type.Reset();MFCreateMediaType(&type);type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_NV12);type->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);MFSetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,output.cx,output.cy);MFSetAttributeRatio(type.Get(),MF_MT_FRAME_RATE,fps,1);MFSetAttributeRatio(type.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1);
    ComPtr<IMFAttributes> encoding;Check(MFCreateAttributes(&encoding,3),"Video encoding settings");
    encoding->SetUINT32(CODECAPI_AVEncCommonRateControlMode,eAVEncCommonRateControlMode_Quality);
    encoding->SetUINT32(CODECAPI_AVEncCommonQuality,VideoQualityValue(quality_preset));
    // Avoid frame reordering shifting the video start relative to the audio clock.
    encoding->SetUINT32(CODECAPI_AVEncMPVDefaultBPictureCount,0);
    if(FAILED(writer_->SetInputMediaType(stream_,type.Get(),encoding.Get()))){
        // Discard any partially applied codec settings. Baseline forbids B frames
        // and restores the previous bitrate path on encoders lacking these options.
        writer_.Reset();Check(MFCreateSinkWriterFromURL(file.c_str(),nullptr,attributes.Get(),&writer_),"Create compatible MP4");
        outputType->SetUINT32(MF_MT_MPEG2_PROFILE,eAVEncH264VProfile_Base);
        outputType->DeleteItem(MF_MT_MPEG_SEQUENCE_HEADER);
        Check(writer_->AddStream(outputType.Get(),&stream_),"Compatible H264 stream");
        Check(writer_->SetInputMediaType(stream_,type.Get(),nullptr),"Configure encoder NV12 input");
    }
    ComPtr<ICodecAPI> videoCodec;
    {
        ComPtr<IMFSinkWriterEx> extended;Check(writer_.As(&extended),"Inspect video encoder");
        bool hardware=false;
        for(DWORD i=0;i<8;++i){
            GUID category{};ComPtr<IMFTransform> transform;
            if(FAILED(extended->GetTransformForStream(stream_,i,&category,&transform)))break;
            ComPtr<IMFAttributes> a;
            if(SUCCEEDED(transform->GetAttributes(&a))){UINT length{};if(SUCCEEDED(a->GetStringLength(MFT_ENUM_HARDWARE_URL_Attribute,&length))&&length)hardware=true;}
            if(category!=MFT_CATEGORY_VIDEO_ENCODER)continue;
            ComPtr<ICodecAPI> codec;if(FAILED(transform.As(&codec)))continue;videoCodec=codec;
            VARIANT cabac{};cabac.vt=VT_BOOL;cabac.boolVal=VARIANT_TRUE;
            codec->SetValue(&CODECAPI_AVEncH264CABACEnable,&cabac);
            // Keep the bounded hardware surface queue and real-time backpressure.
            if(!software){VARIANT latency{};latency.vt=VT_BOOL;latency.boolVal=VARIANT_TRUE;codec->SetValue(&CODECAPI_AVLowLatencyMode,&latency);}
        }
        if(!software&&!hardware)throw std::runtime_error("Hardware H.264 encoder unavailable. Select compatibility encoding explicitly to use the CPU.");
    }
    if(audio_){
        ComPtr<IMFMediaType> a;MFCreateMediaType(&a);a->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio);a->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_AAC);a->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,2);a->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,48000);a->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16);a->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,20000);a->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE,0);
        Check(writer_->AddStream(a.Get(),&audio_stream_),"AAC output");a.Reset();MFCreateMediaType(&a);a->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio);a->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_PCM);a->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,2);a->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,48000);a->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16);a->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,4);a->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,192000);Check(writer_->SetInputMediaType(audio_stream_,a.Get(),nullptr),"PCM input");
    }
    Check(writer_->BeginWriting(),"Start MP4 encoding");
    // Quality is converted to integer QP; the software codec reads 85 back as 86.
    quality_control_=false;
    if(videoCodec){VARIANT mode{},quality{};
        if(SUCCEEDED(videoCodec->GetValue(&CODECAPI_AVEncCommonRateControlMode,&mode))&&
           SUCCEEDED(videoCodec->GetValue(&CODECAPI_AVEncCommonQuality,&quality)))
            quality_control_=mode.vt==VT_UI4&&mode.ulVal==eAVEncCommonRateControlMode_Quality&&quality.vt==VT_UI4&&quality.ulVal+2>=VideoQualityValue(quality_preset)&&quality.ulVal<=VideoQualityValue(quality_preset)+2;
        VariantClear(&mode);VariantClear(&quality);
    }
}
void Encoder::Frame(ID3D11Texture2D* source,RECT crop,long long time,long long duration){
    if(!input_view_||input_source_.Get()!=source){
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC in{};in.ViewDimension=D3D11_VPIV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11VideoProcessorInputView> input;Check(video_->CreateVideoProcessorInputView(source,enumerator_.Get(),&in,&input),"GPU input view");
        input_view_=std::move(input);input_source_=source;
    }
    // Cropping can change independently of the source texture.
    video_context_->VideoProcessorSetStreamSourceRect(processor_.Get(),0,TRUE,&crop);
    D3D11_VIDEO_PROCESSOR_STREAM stream{};stream.Enable=TRUE;stream.pInputSurface=input_view_.Get();Check(video_context_->VideoProcessorBlt(processor_.Get(),output_view_.Get(),0,1,&stream),"GPU crop/scale/NV12");
    ComPtr<IMFMediaBuffer> buffer;ComPtr<IMFSample> sample;
    if(software_){
        if(!staging_){D3D11_TEXTURE2D_DESC d{};output_->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;Check(device_->CreateTexture2D(&d,nullptr,&staging_),"Compatibility staging");}
        context_->CopyResource(staging_.Get(),output_.Get());D3D11_MAPPED_SUBRESOURCE mapped{};Check(context_->Map(staging_.Get(),0,D3D11_MAP_READ,0,&mapped),"Read compatibility frame");
        struct Unmap{ID3D11DeviceContext* c;ID3D11Texture2D* t;~Unmap(){c->Unmap(t,0);}} unmap{context_.Get(),staging_.Get()};
        const DWORD bytes=DWORD(size_.cx*size_.cy*3/2);Check(MFCreateMemoryBuffer(bytes,&buffer),"Compatibility buffer");BYTE* data{};buffer->Lock(&data,nullptr,nullptr);for(LONG y=0;y<size_.cy*3/2;++y)memcpy(data+y*size_.cx,static_cast<BYTE*>(mapped.pData)+y*mapped.RowPitch,size_.cx);buffer->Unlock();buffer->SetCurrentLength(bytes);
    }else{
        const auto slot=pool_->Acquire();bool tracked=false;
        try{context_->CopyResource(pool_->textures[slot].Get(),output_.Get());Check(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D),pool_->textures[slot].Get(),0,FALSE,&buffer),"GPU sample buffer");ComPtr<IMF2DBuffer> two;Check(buffer.As(&two),"GPU buffer layout");DWORD length{};Check(two->GetContiguousLength(&length),"GPU buffer length");Check(buffer->SetCurrentLength(length),"GPU valid bytes");
            Check(MFCreateVideoSampleFromSurface(nullptr,&sample),"Tracked video sample");ComPtr<IMFTrackedSample> tracking;Check(sample.As(&tracking),"Sample release tracking");ComPtr<IMFAsyncCallback> recycle;recycle.Attach(new Recycle(pool_,slot));Check(tracking->SetAllocator(recycle.Get(),nullptr),"Recycle encoder surface");tracked=true;
        }catch(...){if(!tracked)pool_->Release(slot);throw;}
    }
    if(!sample)Check(MFCreateSample(&sample),"Video sample");sample->AddBuffer(buffer.Get());sample->SetSampleTime(time);sample->SetSampleDuration(duration);Check(writer_->WriteSample(stream_,sample.Get()),"Encode frame");
}
void Encoder::Finish(){if(writer_){Check(writer_->Finalize(),"Finalize MP4");writer_.Reset();}}
void Encoder::AudioFrame(const short* data,size_t frames,long long first_frame){
    if(!audio_||!frames)return;ComPtr<IMFMediaBuffer> buffer;const DWORD bytes=static_cast<DWORD>(frames*4);Check(MFCreateMemoryBuffer(bytes,&buffer),"Audio sample memory");BYTE* dest{};Check(buffer->Lock(&dest,nullptr,nullptr),"Audio memory lock");memcpy(dest,data,bytes);buffer->Unlock();buffer->SetCurrentLength(bytes);ComPtr<IMFSample> sample;MFCreateSample(&sample);sample->AddBuffer(buffer.Get());sample->SetSampleTime(first_frame*10000000/48000);sample->SetSampleDuration(static_cast<long long>(frames)*10000000/48000);Check(writer_->WriteSample(audio_stream_,sample.Get()),"Encode audio");
}
}
