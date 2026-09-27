#pragma once
#include <windows.h>
#include <string>
#include <utility>
namespace lumashot {
inline std::wstring ShortcutLabel(UINT mods,UINT key){
    std::wstring s;
    for(auto [flag,name]:{std::pair{MOD_CONTROL,L"Ctrl"}, {MOD_ALT,L"Alt"},{MOD_SHIFT,L"Shift"},{MOD_WIN,L"Win"}})if(mods&flag){if(!s.empty())s+=L" + ";s+=name;}
    if(key){wchar_t name[64]{};UINT scan=MapVirtualKeyW(key,MAPVK_VK_TO_VSC);
        if(key==VK_LEFT||key==VK_RIGHT||key==VK_UP||key==VK_DOWN||key==VK_INSERT||key==VK_DELETE||key==VK_HOME||key==VK_END||key==VK_PRIOR||key==VK_NEXT||key==VK_DIVIDE||key==VK_NUMLOCK)scan|=0x100;
        if(!s.empty())s+=L" + ";
        // Standalone-capable keys whose layout names are missing or misleading
        // (PrtSc reports "Sys Req", Pause has no scan code, F13+ are unnamed).
        if(key==VK_SNAPSHOT)s+=L"PrtSc";else if(key==VK_PAUSE)s+=L"Pause";else if(key>=VK_F13&&key<=VK_F24)s+=L"F"+std::to_wstring(key-VK_F1+1);
        else if(GetKeyNameTextW(static_cast<LONG>(scan<<16),name,64))s+=name;else s+=L"键 "+std::to_wstring(key);
    }return s;
}
}
