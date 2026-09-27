#pragma once
#include <string_view>
namespace lumashot {
inline bool BackgroundLaunch(std::wstring_view argument){return argument==L"--background";}
inline bool CaptureOnLaunch(std::wstring_view argument){return argument.empty()||argument==L"--capture";}
inline bool ActivateExistingOnLaunch(std::wstring_view argument){return !BackgroundLaunch(argument);}
}
