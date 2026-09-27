#pragma once
#include "capture/frame.h"
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>
namespace lumashot {
struct TextFont {std::wstring name,family;};
inline const std::vector<TextFont>& TextFonts(){
    static const auto fonts=[] {
        Microsoft::WRL::ComPtr<IDWriteFactory> factory;
        CheckWin32(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(factory.GetAddressOf()))),"Font list factory");
        Microsoft::WRL::ComPtr<IDWriteFontCollection> collection;CheckWin32(SUCCEEDED(factory->GetSystemFontCollection(&collection)),"Installed fonts");
        const TextFont candidates[]={{L"Segoe UI",L"Segoe UI"},{L"微软雅黑",L"Microsoft YaHei UI"},{L"宋体",L"SimSun"},{L"黑体",L"SimHei"},{L"楷体",L"KaiTi"},{L"仿宋",L"FangSong"},{L"等线",L"DengXian"},{L"Arial",L"Arial"},{L"Times New Roman",L"Times New Roman"},{L"Consolas",L"Consolas"}};
        std::vector<TextFont> result;
        for(const auto& font:candidates){UINT32 index{};BOOL exists{};if(SUCCEEDED(collection->FindFamilyName(font.family.c_str(),&index,&exists))&&exists)result.push_back(font);}
        if(result.empty())result.push_back(candidates[0]);return result;
    }();return fonts;
}
inline std::wstring TextFontName(const std::wstring& family){for(const auto& font:TextFonts())if(font.family==family)return font.name;return family;}
}
