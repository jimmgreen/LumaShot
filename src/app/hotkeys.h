#pragma once
#include "app/preferences.h"
#include <array>
namespace lumashot {
struct Shortcut {UINT modifiers{},key{};bool operator==(const Shortcut&)const=default;};
// Keys that may be bound without Ctrl/Alt/Shift/Win. Character keys always need a
// modifier so a global binding never swallows ordinary typing; F12 stays reserved.
inline bool StandaloneShortcutKey(UINT k){return (k>=VK_F1&&k<=VK_F11)||(k>=VK_F13&&k<=VK_F24)||k==VK_SNAPSHOT||k==VK_PAUSE||k==VK_SCROLL;}
inline bool ValidShortcut(UINT modifiers,UINT key){constexpr UINT mask=MOD_CONTROL|MOD_ALT|MOD_SHIFT|MOD_WIN;return !(modifiers&~mask)&&key<=254&&key!=VK_F12&&(modifiers||StandaloneShortcutKey(key));}
inline std::array<Shortcut,3> Shortcuts(const Preferences& p){return {{{p.modifiers,p.key},{p.gif_modifiers,p.gif_key},{p.video_modifiers,p.video_key}}};}
inline std::array<Shortcut,4> AllShortcuts(const Preferences& p){return {{{p.modifiers,p.key},{p.gif_modifiers,p.gif_key},{p.video_modifiers,p.video_key},{p.clipboard_modifiers,p.clipboard_key}}};}
inline bool UniqueShortcuts(const Preferences& p){const auto a=AllShortcuts(p);for(size_t i=0;i<a.size();++i)for(size_t j=0;j<i;++j)if(a[i].key&&a[i]==a[j])return false;return true;}
// The caller retains the old configuration if this transaction fails.
inline bool ApplyShortcuts(HWND window,const Preferences& old,const Preferences& next){
    if(!UniqueShortcuts(next))return false;
    const auto before=Shortcuts(old),after=Shortcuts(next);
    for(int i=0;i<3;++i)if(before[i]!=after[i])UnregisterHotKey(window,i+1);
    bool ok=true;
    for(int i=0;i<3;++i)if(before[i]!=after[i]&&after[i].key&&!RegisterHotKey(window,i+1,after[i].modifiers|MOD_NOREPEAT,after[i].key))ok=false;
    if(ok)return true;
    for(int i=0;i<3;++i)if(before[i]!=after[i])UnregisterHotKey(window,i+1);
    for(int i=0;i<3;++i)if(before[i]!=after[i]&&before[i].key)RegisterHotKey(window,i+1,before[i].modifiers|MOD_NOREPEAT,before[i].key);
    return false;
}
}
