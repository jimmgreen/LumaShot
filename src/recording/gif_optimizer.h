#pragma once
#include <filesystem>
#include <functional>
#include <stop_token>
#include <cstdint>
namespace lumashot::recording {
// Optional whole-animation optimization. Failure keeps the original file.
void OptimizeGif(const std::filesystem::path& file,bool loop,std::stop_token stop,const std::function<void(int)>& progress);
namespace detail {
// Internal seam for deterministic subprocess/fallback tests, not a UI setting.
struct GifOptimizerTool {
    std::filesystem::path executable;
    unsigned timeout_ms{30000};
    size_t memory_bytes{256ull*1024*1024};
};
void OptimizeGifWithTool(const std::filesystem::path& file,bool loop,std::stop_token stop,
    const std::function<void(int)>& progress,const GifOptimizerTool& tool);
}
}
