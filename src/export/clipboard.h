#pragma once
#include "capture/frame.h"
#include <string>
#include <filesystem>
#include <memory>
#include <chrono>
#include <windows.h>
namespace lumashot {
struct ClipboardFile {
    std::filesystem::path path;bool published{};
    ~ClipboardFile(){if(!published&&!path.empty()){std::error_code error;std::filesystem::remove(path,error);}}
};
struct ClipboardImage {
    HGLOBAL image{},drop{};std::shared_ptr<ClipboardFile> file;
    ClipboardImage()=default;
    ClipboardImage(const ClipboardImage&)=delete;
    ClipboardImage& operator=(const ClipboardImage&)=delete;
    ~ClipboardImage(){if(image)GlobalFree(image);if(drop)GlobalFree(drop);}
};
std::shared_ptr<ClipboardFile> PrepareClipboardFile(const Frame&,int format);
std::shared_ptr<ClipboardFile> PrepareClipboardFile(const PixelView&,int format);
// Deletes %TEMP%\LumaShot-Clipboard\LumaShot-* files older than age, except keep.
size_t CleanupClipboardFiles(std::chrono::hours age=std::chrono::hours(24),const std::filesystem::path& keep={});
// First CF_HDROP path currently on the clipboard, or empty.
std::filesystem::path CurrentClipboardFile(HWND owner);
std::unique_ptr<ClipboardImage> PrepareClipboardImage(const Frame&,const std::shared_ptr<ClipboardFile>& file={});
// Views publish a crop without an intermediate copy.
std::unique_ptr<ClipboardImage> PrepareClipboardImage(const PixelView&,const std::shared_ptr<ClipboardFile>& file={});
void PublishClipboardImage(HWND owner,ClipboardImage&);
void CopyImage(HWND owner,const Frame& frame,const std::shared_ptr<ClipboardFile>& file={});
void CopyText(HWND owner,const std::wstring& text);
}