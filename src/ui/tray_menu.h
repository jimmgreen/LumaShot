#pragma once
#include <windows.h>
namespace lumashot::ui {
// The caller owns the native command model; this popup only selects a command.
UINT TrackTrayMenu(HWND owner,HMENU commands,POINT anchor,bool dark);
}
