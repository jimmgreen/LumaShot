#pragma once
#include "recording/core.h"
namespace lumashot::recording {
// Transactional GIF export used by the recording UI. 100 means saved, not
// merely encoded; cancellation never replaces the destination.
void ExportGifToFile(const std::filesystem::path& source,const std::filesystem::path& destination,
    const GifOptions& options,std::stop_token stop,const std::function<void(int)>& progress);
}
