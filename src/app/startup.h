#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
namespace lumashot {
inline std::wstring LoginStartupCommand(const std::filesystem::path& executable){return L"\""+executable.native()+L"\" --background";}
inline bool SetLoginStartup(bool enabled,const std::filesystem::path& executable,LPCWSTR subkey=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"){
    HKEY key{};if(RegCreateKeyExW(HKEY_CURRENT_USER,subkey,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
    const auto command=LoginStartupCommand(executable);
    const LSTATUS status=enabled?RegSetValueExW(key,L"LumaShot",0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t))):RegDeleteValueW(key,L"LumaShot");
    RegCloseKey(key);return status==ERROR_SUCCESS||(!enabled&&status==ERROR_FILE_NOT_FOUND);
}
inline bool ConfigureLoginStartup(bool enabled){wchar_t path[32768]{};const DWORD length=GetModuleFileNameW(nullptr,path,32768);return length&&length<32768&&SetLoginStartup(enabled,path);}
}
