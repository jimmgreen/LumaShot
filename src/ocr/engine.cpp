#include "ocr/engine.h"
#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <thread>
#include <chrono>

namespace lumashot::ocr {
// The serial worker reuses one allocator across detection and recognition.
// It belongs to the worker process, which exits after 30 seconds idle.
static Ort::Env& Runtime() {
    struct SharedRuntime {
        Ort::Env env{ORT_LOGGING_LEVEL_WARNING,"LumaShot OCR"};
        SharedRuntime() {
            auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
            Ort::ArenaCfg arena(0,1,-1,-1);
            env.CreateAndRegisterAllocator(memory,arena);
        }
    };
    static SharedRuntime runtime;return runtime.env;
}
struct Engine::Impl {
    Ort::Env& env{Runtime()};
    Ort::SessionOptions options;
    Ort::Session detector{nullptr},recognizer{nullptr};
    std::vector<std::wstring> dictionary;
    Impl(const std::filesystem::path& folder) {
        options.SetIntraOpNumThreads(static_cast<int>(std::clamp(std::thread::hardware_concurrency(),1u,2u)));
        options.SetInterOpNumThreads(1);
        options.AddConfigEntry("session.intra_op.allow_spinning","0");
        options.AddConfigEntry("session.inter_op.allow_spinning","0");
        options.EnableCpuMemArena();options.DisableMemPattern();
        options.AddConfigEntry("session.use_env_allocators","1");
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        detector=Ort::Session(env,(folder/L"det.onnx").c_str(),options);
        recognizer=Ort::Session(env,(folder/L"rec.onnx").c_str(),options);
        std::ifstream file(folder/L"dictionary.txt",std::ios::binary);std::string line;
        while(std::getline(file,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();dictionary.push_back(Utf16(line));}
        if(dictionary.size()!=18708)throw std::runtime_error("Missing or incompatible OCR dictionary");
        dictionary.push_back(L" ");
    }
    static Ort::Value Run(Ort::Session& session,std::vector<float>& input,int h,int w) {
        const std::array<int64_t,4> shape{1,3,h,w};
        auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
        auto tensor=Ort::Value::CreateTensor<float>(memory,input.data(),input.size(),shape.data(),shape.size());
        Ort::AllocatorWithDefaultOptions alloc;
        auto input_name=session.GetInputNameAllocated(0,alloc),output_name=session.GetOutputNameAllocated(0,alloc);
        const char* inputs[]={input_name.get()};const char* outputs[]={output_name.get()};
        auto result=session.Run(Ort::RunOptions{nullptr},inputs,&tensor,1,outputs,1);
        return std::move(result.front());
    }
};
Engine::Engine(const std::filesystem::path& assets):impl_(std::make_unique<Impl>(assets)){}
Engine::~Engine()=default;
// Precompute horizontal weights and fetch each BGRA pixel once for all three
// BGR planes. The interpolation and model normalization stay unchanged.
static std::vector<float> Input(const Frame& f,Box b,int h,int w,bool detection,int content_width=0) {
    const std::array<float,3> mean{0.485f,0.456f,0.406f},sd{0.229f,0.224f,0.225f};
    std::vector<float> values(static_cast<size_t>(3)*h*w,0);
    if(!content_width)content_width=w;
    struct SampleX {int left,right;float fraction;};std::vector<SampleX> xs(static_cast<size_t>(content_width));
    for(int x=0;x<content_width;++x){const float xx=std::clamp(b.left+(float(x)+0.5f)*(b.right-b.left)/float(content_width)-0.5f,0.0f,float(f.Width()-1));xs[x]={int(xx),std::min(int(xx)+1,f.Width()-1),xx-float(int(xx))};}
    for(int y=0;y<h;++y) {
        const float yy=std::clamp(b.top+(float(y)+0.5f)*(b.bottom-b.top)/float(h)-0.5f,0.0f,float(f.Height()-1));
        const int y0=int(yy),y1=std::min(y0+1,f.Height()-1);const float fy=yy-float(y0);
        const auto* top=f.pixels.data()+static_cast<size_t>(y0)*f.Width();const auto* bottom=f.pixels.data()+static_cast<size_t>(y1)*f.Width();
        for(int x=0;x<content_width;++x) {
            const auto sx=xs[x];const uint32_t a=top[sx.left],bb=top[sx.right],c=bottom[sx.left],d=bottom[sx.right];
            for(int channel=0;channel<3;++channel) {
                const int shift=channel*8;
                const float av=float((a>>shift)&255)/255.0f,bv=float((bb>>shift)&255)/255.0f,cv=float((c>>shift)&255)/255.0f,dv=float((d>>shift)&255)/255.0f;
                const float v=(av*(1-sx.fraction)+bv*sx.fraction)*(1-fy)+(cv*(1-sx.fraction)+dv*sx.fraction)*fy;
                values[(static_cast<size_t>(channel)*h+y)*w+x]=detection?(v-mean[channel])/sd[channel]:(v-0.5f)*2;
            }
        }
    }return values;
}
static float Area(Box b){return std::max(0.0f,b.right-b.left)*std::max(0.0f,b.bottom-b.top);}
Text Engine::Recognize(const Frame& frame,Timings* timings) {
    if(timings)*timings={};
    using Clock=std::chrono::steady_clock;
    Timings measured;
    const auto elapsed=[](Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();};
    std::vector<Box> boxes;
    constexpr int tile=960,overlap=128;
    // Trim only a uniform outer background, retaining padding for detection.
    // Every non-background pixel participates, including faint antialiasing.
    int x0=frame.Width(),y0=frame.Height(),x1=0,y1=0;
    const uint32_t background=frame.pixels.front()&0xffffff;
    for(int y=0;y<frame.Height();++y)for(int x=0;x<frame.Width();++x)
        if((frame.pixels[static_cast<size_t>(y)*frame.Width()+x]&0xffffff)!=background){x0=std::min(x0,x);y0=std::min(y0,y);x1=std::max(x1,x+1);y1=std::max(y1,y+1);}
    if(x0==frame.Width())return {};
    x0=std::max(0,x0-32);y0=std::max(0,y0-32);x1=std::min(frame.Width(),x1+32);y1=std::min(frame.Height(),y1+32);
    // Overlap preserves lines crossing a tile boundary. Larger duplicate boxes win.
    for(int top=y0;top<y1;top+=tile-overlap)for(int left=x0;left<x1;left+=tile-overlap) {
        const int tw=std::min(tile,x1-left),th=std::min(tile,y1-top);
        const int w=(tw+31)/32*32,h=(th+31)/32*32;
        const Box region{float(left),float(top),float(left+tw),float(top+th)};
        auto start=Clock::now();auto input=Input(frame,region,h,w,true);measured.preprocess_ms+=elapsed(start);
        start=Clock::now();auto result=Impl::Run(impl_->detector,input,h,w);measured.detection_ms+=elapsed(start);
        const auto shape=result.GetTensorTypeAndShapeInfo().GetShape();
        if(shape.size()!=4||shape[0]!=1||shape[1]!=1||shape[2]<1||shape[3]<1)throw std::runtime_error("Unexpected OCR detection output");
        const int oh=static_cast<int>(shape[2]),ow=static_cast<int>(shape[3]);
        const auto* map=result.GetTensorData<float>();
        std::vector<uint8_t> visited(static_cast<size_t>(oh)*ow);std::vector<int> queue;
        for(int y=0;y<oh;++y)for(int x=0;x<ow;++x) {
            const int index=y*ow+x;
            if(visited[index]||map[index]<0.2f)continue;
            queue.clear();queue.push_back(index);visited[index]=1;
            int xmin=x,xmax=x,ymin=y,ymax=y;float score=0;
            for(size_t i=0;i<queue.size();++i) {
                const int p=queue[i],px=p%ow,py=p/ow;score+=map[p];
                xmin=std::min(xmin,px);xmax=std::max(xmax,px);ymin=std::min(ymin,py);ymax=std::max(ymax,py);
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                    const int nx=px+dx,ny=py+dy,n=ny*ow+nx;
                    if(nx<0||ny<0||nx>=ow||ny>=oh||visited[n]||map[n]<0.2f)continue;
                    visited[n]=1;queue.push_back(n);
                }
            }
            if(queue.size()<6||score/float(queue.size())<0.45f)continue;
            const float bw=float(xmax-xmin+1),bh=float(ymax-ymin+1);
            const float expand=bw*bh*1.4f/(2*(bw+bh));
            Box box{left+(xmin-expand)*float(tw)/float(ow),top+(ymin-expand)*float(th)/float(oh),
                left+(xmax+1+expand)*float(tw)/float(ow),top+(ymax+1+expand)*float(th)/float(oh)};
            box.left=std::max(0.0f,box.left);box.top=std::max(0.0f,box.top);
            box.right=std::min(float(frame.Width()),box.right);box.bottom=std::min(float(frame.Height()),box.bottom);
            if(box.right-box.left>=3&&box.bottom-box.top>=3)boxes.push_back(box);
            if(boxes.size()>10000)throw std::runtime_error("Too many text regions; please use a smaller selection");
        }
    }
    // Join overlapping fragments from adjacent tiles before recognizing the
    // original-resolution line; NMS alone would discard a clipped line ending.
    for(size_t i=0;i<boxes.size();++i)for(size_t j=i+1;j<boxes.size();) {
        auto& a=boxes[i];const auto b=boxes[j];
        const float vertical=std::min(a.bottom,b.bottom)-std::max(a.top,b.top);
        const float horizontal=std::min(a.right,b.right)-std::max(a.left,b.left);
        if(horizontal>0&&vertical>0.75f*std::max(a.bottom-a.top,b.bottom-b.top)) {
            a={std::min(a.left,b.left),std::min(a.top,b.top),std::max(a.right,b.right),std::max(a.bottom,b.bottom)};
            boxes.erase(boxes.begin()+static_cast<ptrdiff_t>(j));
        }else ++j;
    }
    std::sort(boxes.begin(),boxes.end(),[](Box a,Box b){return Area(a)>Area(b);});
    std::vector<Box> unique;
    for(const auto b:boxes) {
        bool duplicate=false;
        for(const auto a:unique) {
            const Box intersection{std::max(a.left,b.left),std::max(a.top,b.top),std::min(a.right,b.right),std::min(a.bottom,b.bottom)};
            if(Area(intersection)>0.65f*std::min(Area(a),Area(b))){duplicate=true;break;}
        }if(!duplicate)unique.push_back(b);
    }
    Text text;
    for(const auto box:unique) {
        const int content=std::clamp(int(std::ceil(48*(box.right-box.left)/(box.bottom-box.top))),8,3200);
        const int width=std::max(128,(content+7)/8*8);
        auto start=Clock::now();auto input=Input(frame,box,48,width,false,content);measured.preprocess_ms+=elapsed(start);
        start=Clock::now();auto output=Impl::Run(impl_->recognizer,input,48,width);measured.recognition_ms+=elapsed(start);
        const auto shape=output.GetTensorTypeAndShapeInfo().GetShape();
        if(shape.size()!=3||shape[0]!=1)throw std::runtime_error("Unexpected OCR recognition shape");
        start=Clock::now();auto line=DecodeCtc({output.GetTensorData<float>(),output.GetTensorTypeAndShapeInfo().GetElementCount()},
            static_cast<size_t>(shape[1]),static_cast<size_t>(shape[2]),impl_->dictionary,box,float(content)/float(width));
        measured.decode_ms+=elapsed(start);
        if(!line.glyphs.empty()&&line.confidence>=0.45f)text.lines.push_back(std::move(line));
    }
    ReadingOrder(text);if(timings)*timings=measured;return text;
}
}
