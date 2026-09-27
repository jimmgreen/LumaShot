#pragma once
#include <windows.h>
#include <array>
#include <filesystem>
#include <system_error>

namespace lumashot::ocr {
inline constexpr std::array<const wchar_t*,5> RequiredAssets{
    L"lumashot_ocr_worker.exe",L"onnxruntime.dll",L"ocr/det.onnx",L"ocr/rec.onnx",L"ocr/dictionary.txt"};
inline bool Available(const std::filesystem::path& directory) {
    for(const auto* asset:RequiredAssets){
        std::error_code error;const auto path=directory/asset;
        if(!std::filesystem::is_regular_file(path,error)||error)return false;
        const auto size=std::filesystem::file_size(path,error);if(error||size==0)return false;
    }
    return true;
}
inline bool Available() {
    std::array<wchar_t,32768> module{};
    const DWORD length=GetModuleFileNameW(nullptr,module.data(),static_cast<DWORD>(module.size()));
    return length>0&&length<module.size()&&Available(std::filesystem::path(module.data()).parent_path());
}
}
