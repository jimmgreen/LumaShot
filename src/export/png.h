#pragma once
#include "capture/frame.h"
#include <filesystem>

namespace lumashot {
void SavePng(const Frame& frame, const std::filesystem::path& path);
void SaveImageFile(const Frame& frame,const std::filesystem::path& path,int format);
void SaveClipboardImageFile(const Frame& frame,const std::filesystem::path& path,int format);
// Views encode a crop in place (no intermediate copy of the pixels).
void SaveImageFile(const PixelView& view,const std::filesystem::path& path,int format);
void SaveClipboardImageFile(const PixelView& view,const std::filesystem::path& path,int format);
Frame ReadPng(const std::filesystem::path& path);
}


