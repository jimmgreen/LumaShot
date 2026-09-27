#include "ui/text_renderer.h"
#include <wrl/client.h>
#include <stdexcept>
#include <string>
#include <system_error>
#include "model/selection.h"
#include <algorithm>
#include <cmath>

namespace lumashot {
struct TextRenderer::Context {
    Microsoft::WRL::ComPtr<IDWriteFactory> factory;
    LumaText::Context handle;
};
namespace {
void Check(lt_result result) {
    if(result != LT_OK) throw std::runtime_error(std::string("LumaText: ")+lt_result_string(result));
}
}
lt_frame_stats TextRenderer::Draw(ID2D1RenderTarget* target, IDWriteFactory* factory,
    std::wstring_view text, IDWriteTextFormat* format, D2D1_RECT_F bounds,
    D2D1_COLOR_F color, bool clip) {
    auto stats=LumaText::Descriptor<lt_frame_stats>();
    if(text.empty() || bounds.right<=bounds.left || bounds.bottom<=bounds.top)return stats;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    const auto hr=factory->CreateTextLayout(text.data(),static_cast<UINT32>(text.size()),format,
        bounds.right-bounds.left,bounds.bottom-bounds.top,&layout);
    if(FAILED(hr))throw std::system_error(static_cast<int>(hr),std::system_category(),"Text layout");
    return DrawLayout(target,factory,layout.Get(),{bounds.left,bounds.top},bounds,color,clip);
}
lt_frame_stats TextRenderer::DrawLayout(ID2D1RenderTarget* target, IDWriteFactory* factory,
    IDWriteTextLayout* layout,D2D1_POINT_2F origin,D2D1_RECT_F bounds,D2D1_COLOR_F color,bool clip){
    auto stats=LumaText::Descriptor<lt_frame_stats>();
    if(!layout)return stats;
    if(!context_) {
        // Repeatedly destroying warmed contexts in the packaged rasterizer
        // retains per-context allocations. Keep one bounded CPU context per
        // UI/export thread, never its renderer, target, frame, or screenshot.
        static thread_local std::shared_ptr<Context> shared;
        if(!shared) {
            if(lt_get_abi_version()!=LT_ABI_VERSION)throw std::runtime_error("LumaText ABI mismatch");
            auto created=std::make_shared<Context>();created->factory=factory;
            auto desc=LumaText::Descriptor<lt_context_desc>();
            desc.dwrite_factory=factory;desc.cpu_cache_limit_bytes=16ull*1024*1024;
            Check(lt_context_create(&desc,created->handle.put()));
            shared=std::move(created);
        }
        context_=shared;
    }
    auto renderer_desc=LumaText::Descriptor<lt_d2d_desc>();
    renderer_desc.render_target=target;renderer_desc.manage_begin_end_draw=false;
    LumaText::Renderer renderer;Check(lt_d2d_renderer_create(context_->handle.get(),&renderer_desc,renderer.put()));
    auto frame_desc=LumaText::Descriptor<lt_frame_desc>();
    target->GetDpi(&frame_desc.dpi_x,&frame_desc.dpi_y);
    LumaText::Frame frame;Check(lt_frame_begin(renderer.get(),&frame_desc,frame.put()));
    auto draw=LumaText::Descriptor<lt_draw_text_desc>();
    draw.origin_x=origin.x;draw.origin_y=origin.y;
    draw.clip={bounds.left,bounds.top,bounds.right,bounds.bottom};draw.clip_enabled=clip;
    draw.foreground={color.r,color.g,color.b,color.a};
    draw.render_config=LumaText::Descriptor<lt_render_config>();
    draw.render_config.coverage_gamma=.85f;draw.render_config.coverage_contrast=1.f;
    const auto result=lt_frame_draw_layout(frame.get(),layout,&draw);
    const auto end=lt_frame_end(frame.get());
    Check(result);Check(end);Check(lt_frame_get_stats(frame.get(),&stats));
    return stats;
}
}

