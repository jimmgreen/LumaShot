#pragma once
#include "clipboard/history.h"
#include "capture/frame.h"
#include <memory>

namespace lumashot::clipboard {
// Only bounded display data crosses back to the UI; never raw clipboard formats.
struct PreviewData {
    static constexpr UINT MaxEdge = 640;
    static constexpr uint64_t MaxSourcePixels = 32ull * 1024 * 1024;
    static constexpr size_t MaxTextChars = 65536;
    static constexpr size_t MaxFileChars = 256 * 272;
    bool truncated{};
    uint32_t file_count{};
    std::shared_ptr<const Frame> image;
    std::wstring text;
    std::wstring error;
};
// Call on the session worker for disk-backed entries, not from WM_PAINT.
PreviewData PreparePreview(const Entry& entry);
}
