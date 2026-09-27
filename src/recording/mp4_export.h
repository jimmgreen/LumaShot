#pragma once
#include <filesystem>
#include <functional>
#include <stop_token>
#include <cstdint>
namespace lumashot::recording {
// Percent is local to the named phase, not an estimate of total time.
// -1 means work is indeterminate; Complete is emitted only after atomic save.
enum class Mp4Stage {Inspect,Encode,Verify,Save,Complete};
struct Mp4StageProgress {Mp4Stage stage;int percent;int attempt;};
using Mp4StageCallback=std::function<void(const Mp4StageProgress&)>;
enum class Mp4Encoding {Original,H264,Av1};
struct Mp4ExportResult {Mp4Encoding encoding{Mp4Encoding::Original};uintmax_t original_bytes{},saved_bytes{};};
Mp4ExportResult ExportMp4ToFile(const std::filesystem::path& source,const std::filesystem::path& destination,
    std::stop_token stop,const std::function<void(int)>& progress,const Mp4StageCallback& stage={});
namespace detail {
struct Mp4OptimizerTool {std::filesystem::path executable;unsigned timeout_ms{900000};size_t memory_bytes{3ull*1024*1024*1024};bool prefer_av1{true};};
Mp4ExportResult ExportMp4WithTool(const std::filesystem::path& source,const std::filesystem::path& destination,
    std::stop_token stop,const std::function<void(int)>& progress,const Mp4OptimizerTool& tool,const Mp4StageCallback& stage={});
}
}
