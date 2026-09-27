#pragma once
#include "ocr/text.h"
#include "capture/frame.h"
#include <filesystem>
#include <memory>
namespace lumashot::ocr {
struct Timings { double preprocess_ms{},detection_ms{},recognition_ms{},decode_ms{}; };
class Engine {
public:
    explicit Engine(const std::filesystem::path& assets);
    ~Engine();
    Text Recognize(const Frame& frame,Timings* timings=nullptr);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
