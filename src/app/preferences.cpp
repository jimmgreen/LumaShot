#include "app/preferences.h"
#include "app/hotkeys.h"
#include "app/tool_preferences.h"
#include "app/settings_process.h"
#include <commctrl.h>
#include <shlobj.h>
#include <functional>
#include <string>
#include <algorithm>
#include <stdexcept>

namespace lumashot {
namespace {
// WritePrivateProfileStringW keeps an existing file's encoding and creates new files
// as ANSI; a UTF-16 LE BOM makes it store non-ANSI folders and font names intact.
bool HasUnicodeBom(const std::filesystem::path& path){
    const HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    unsigned char bom[2]{};DWORD read{};const bool ok=ReadFile(file,bom,2,&read,nullptr)&&read==2&&bom[0]==0xFF&&bom[1]==0xFE;
    CloseHandle(file);return ok;
}
void CreateUnicodeIni(const std::filesystem::path& path){
    const HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Unable to create settings transaction");
    const unsigned char bom[2]{0xFF,0xFE};DWORD written{};const bool ok=WriteFile(file,bom,2,&written,nullptr)&&written==2;
    CloseHandle(file);if(!ok)throw std::runtime_error("Unable to write settings transaction");
}
std::wstring ReadProfile(const std::function<DWORD(wchar_t*,DWORD)>& read){
    for(DWORD size=4096;size<=(1u<<24);size*=2){std::wstring buffer(size,L'\0');const DWORD used=read(buffer.data(),size);
        if(used<size-2){buffer.resize(used);return buffer;}}
    throw std::runtime_error("Settings file is too large");
}
// Rewrites an ANSI settings file into a Unicode transaction file, section by section.
void CopyAsUnicodeIni(const std::filesystem::path& source,const std::filesystem::path& target){
    CreateUnicodeIni(target);
    const auto names=ReadProfile([&](wchar_t* b,DWORD n){return GetPrivateProfileSectionNamesW(b,n,source.c_str());});
    for(size_t start=0;start<names.size()&&names[start];){
        const std::wstring section(names.c_str()+start);start+=section.size()+1;
        auto values=ReadProfile([&](wchar_t* b,DWORD n){return GetPrivateProfileSectionW(section.c_str(),b,n,source.c_str());});
        values.push_back(L'\0');values.push_back(L'\0');
        if(!WritePrivateProfileSectionW(section.c_str(),values.c_str(),target.c_str()))throw std::runtime_error("Unable to migrate settings");
    }
}
}
static std::filesystem::path ConfigPath() {
    PWSTR raw{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&raw))) return {};
    std::filesystem::path path(raw);
    CoTaskMemFree(raw);
    return path/L"LumaShot"/L"settings.ini";
}
Preferences Preferences::Load() {
    return LoadFrom(ConfigPath());
}
Preferences Preferences::LoadFrom(const std::filesystem::path& path) {
    Preferences value;
    if(path.empty()) return value;
    value.tools=LoadToolProperties(path,value.tools);
    value.start_with_windows=GetPrivateProfileIntW(L"General",L"StartWithWindows",1,path.c_str())!=0;
    value.include_cursor=GetPrivateProfileIntW(L"General",L"IncludeCursor",1,path.c_str())!=0;
    value.clipboard_enabled=GetPrivateProfileIntW(L"General",L"ClipboardEnabled",0,path.c_str())!=0;
    value.clipboard_persist=GetPrivateProfileIntW(L"General",L"ClipboardPersist",0,path.c_str())!=0;
    value.clipboard_strip_visible=GetPrivateProfileIntW(L"General",L"ClipboardStripVisible",1,path.c_str())!=0;
    value.clipboard_strip_hint_shown=GetPrivateProfileIntW(L"General",L"ClipboardStripHintShown",0,path.c_str())!=0;
    value.translate_auto_show=GetPrivateProfileIntW(L"General",L"TranslateAutoShow",1,path.c_str())!=0;
    value.settings_page=std::clamp(static_cast<int>(GetPrivateProfileIntW(L"General",L"SettingsPage",0,path.c_str())),0,5);
    value.hotkeys_disabled=GetPrivateProfileIntW(L"General",L"HotkeysDisabled",0,path.c_str())!=0;
    value.disable_hotkeys_in_game=GetPrivateProfileIntW(L"General",L"DisableHotkeysInGame",0,path.c_str())!=0;
    value.paste_as_file=GetPrivateProfileIntW(L"General",L"PasteAsFile",1,path.c_str())!=0;
    value.paste_file_format=std::clamp(static_cast<int>(GetPrivateProfileIntW(L"General",L"PasteFileFormat",0,path.c_str())),0,2);
    value.pin_style=NormalizePinStyle(static_cast<int>(GetPrivateProfileIntW(L"General",L"PinStyle",static_cast<int>(DefaultPinStyle),path.c_str())));
    value.theme=std::clamp(static_cast<int>(GetPrivateProfileIntW(L"General",L"Theme",0,path.c_str())),0,2);
    value.key=GetPrivateProfileIntW(L"General",L"Hotkey",'A',path.c_str());
    value.modifiers=GetPrivateProfileIntW(L"General",L"Modifiers",MOD_CONTROL|MOD_ALT,path.c_str());
    if(!ValidShortcut(value.modifiers,value.key)) {
        value.key='A';value.modifiers=MOD_CONTROL|MOD_ALT;
    }
    const auto shortcut=[&](LPCWSTR key,LPCWSTR mods,UINT fallback,UINT& k,UINT& m){
        k=GetPrivateProfileIntW(L"General",key,fallback,path.c_str());m=GetPrivateProfileIntW(L"General",mods,MOD_CONTROL|MOD_ALT,path.c_str());
        if(k&&!ValidShortcut(m,k)){k=fallback;m=MOD_CONTROL|MOD_ALT;}
    };
    shortcut(L"GifHotkey",L"GifModifiers",'G',value.gif_key,value.gif_modifiers);
    shortcut(L"VideoHotkey",L"VideoModifiers",'R',value.video_key,value.video_modifiers);
    value.clipboard_key=GetPrivateProfileIntW(L"General",L"ClipboardHotkey",'V',path.c_str());
    value.clipboard_modifiers=GetPrivateProfileIntW(L"General",L"ClipboardModifiers",MOD_WIN,path.c_str());
    if(value.clipboard_key&&!ValidShortcut(value.clipboard_modifiers,value.clipboard_key)){value.clipboard_key='V';value.clipboard_modifiers=MOD_WIN;}
    value.translate_key=GetPrivateProfileIntW(L"General",L"TranslateHotkey",'Y',path.c_str());
    value.translate_modifiers=GetPrivateProfileIntW(L"General",L"TranslateModifiers",MOD_CONTROL|MOD_ALT,path.c_str());
    if(value.translate_key&&!ValidShortcut(value.translate_modifiers,value.translate_key)){value.translate_key='Y';value.translate_modifiers=MOD_CONTROL|MOD_ALT;}
    wchar_t folder[32768]{};
    GetPrivateProfileStringW(L"General",L"Folder",L"",folder,32768,path.c_str());
    value.save_directory=folder;
    value.update_auto_check=GetPrivateProfileIntW(L"General",L"UpdateAutoCheck",1,path.c_str())!=0;
    wchar_t text[2048]{};
    GetPrivateProfileStringW(L"General",L"UpdateLastCheck",L"0",text,64,path.c_str());
    value.update_last_check=std::max(0LL,_wtoi64(text));
    GetPrivateProfileStringW(L"General",L"UpdateMirrors",L"",text,2048,path.c_str());
    value.update_mirrors=text;
    GetPrivateProfileStringW(L"General",L"LastRunVersion",L"",text,64,path.c_str());
    value.last_run_version=text;
    return value;
}
void Preferences::Save() const {
    SaveTo(ConfigPath());
}
void Preferences::SaveTo(const std::filesystem::path& destination) const {
    const auto& path=destination;
    if(path.empty()) return;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(),error);
    if(error) throw std::system_error(error,"Create preferences directory");
    GUID id{};if(FAILED(CoCreateGuid(&id)))throw std::runtime_error("Create settings transaction");
    wchar_t suffix[40]{};StringFromGUID2(id,suffix,40);
    const auto temporary=std::filesystem::path(path.native()+suffix+L".tmp");
    struct RemoveTemporary{std::filesystem::path path;~RemoveTemporary(){std::error_code ec;std::filesystem::remove(path,ec);}} cleanup{temporary};
    if(!std::filesystem::exists(path))CreateUnicodeIni(temporary);
    else if(HasUnicodeBom(path))std::filesystem::copy_file(path,temporary);
    else CopyAsUnicodeIni(path,temporary);
    const auto write=[&](LPCWSTR key,const std::wstring& value) {
        if(!WritePrivateProfileStringW(L"General",key,value.c_str(),temporary.c_str()))
            throw std::runtime_error("Unable to save settings");
    };
    write(L"StartWithWindows",start_with_windows?L"1":L"0");
    write(L"IncludeCursor",include_cursor?L"1":L"0");
    write(L"ClipboardEnabled",clipboard_enabled?L"1":L"0");
    write(L"ClipboardPersist",clipboard_persist?L"1":L"0");
    write(L"ClipboardStripVisible",clipboard_strip_visible?L"1":L"0");
    write(L"ClipboardStripHintShown",clipboard_strip_hint_shown?L"1":L"0");
    write(L"TranslateAutoShow",translate_auto_show?L"1":L"0");
    write(L"SettingsPage",std::to_wstring(std::clamp(settings_page,0,5)));
    write(L"HotkeysDisabled",hotkeys_disabled?L"1":L"0");
    write(L"DisableHotkeysInGame",disable_hotkeys_in_game?L"1":L"0");
    write(L"PasteAsFile",paste_as_file?L"1":L"0");
    write(L"PasteFileFormat",std::to_wstring(std::clamp(paste_file_format,0,2)));
    write(L"PinStyle",std::to_wstring(static_cast<int>(NormalizePinStyle(static_cast<int>(pin_style)))));
    write(L"Theme",std::to_wstring(theme));write(L"Hotkey",std::to_wstring(key));
    write(L"GifHotkey",std::to_wstring(gif_key));write(L"GifModifiers",std::to_wstring(gif_modifiers));
    write(L"VideoHotkey",std::to_wstring(video_key));write(L"VideoModifiers",std::to_wstring(video_modifiers));
    write(L"ClipboardHotkey",std::to_wstring(clipboard_key));write(L"ClipboardModifiers",std::to_wstring(clipboard_modifiers));write(L"TranslateHotkey",std::to_wstring(translate_key));write(L"TranslateModifiers",std::to_wstring(translate_modifiers));
    SaveToolProperties(temporary,tools);
    // Remove obsolete tool selection while preserving all style preferences.
    if(!WritePrivateProfileStringW(L"General",L"LastTool",nullptr,temporary.c_str()))
        throw std::runtime_error("Unable to remove obsolete last tool setting");
    write(L"Modifiers",std::to_wstring(modifiers));write(L"Folder",save_directory.native());
    write(L"UpdateAutoCheck",update_auto_check?L"1":L"0");write(L"UpdateLastCheck",std::to_wstring(update_last_check));
    write(L"UpdateMirrors",update_mirrors);write(L"LastRunVersion",last_run_version);
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,temporary.c_str());
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Unable to commit settings");
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,path.c_str());
}
bool Preferences::Dark() const {
    if(theme!=0) return theme==2;
    DWORD light=1,bytes=sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&bytes);
    return light==0;
}
bool EditPreferences(HWND owner,Preferences& preferences,const std::function<bool(const Preferences&)>& accept) {
    return EditPreferencesIsolated(owner,preferences,accept);
}
}