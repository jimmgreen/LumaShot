#pragma once
#include <windows.h>

// Screenshot-translation settings window. It runs in its own short-lived
// process (`LumaShot.exe --translation-settings [--dark]`), writes
// %LOCALAPPDATA%\LumaShot\translation.ini directly (keys DPAPI-protected) and
// notifies the tray host so waiting pins retry. Nothing stays resident.
namespace lumashot {
inline constexpr UINT kTranslationConfigChanged = WM_APP + 124;
int TranslationSettingsMain(bool dark);
// Brings an open settings window forward or starts the settings process.
bool LaunchTranslationSettings(bool dark);
}
