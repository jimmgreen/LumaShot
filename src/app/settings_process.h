#pragma once
#include "app/preferences.h"
#include <span>
namespace lumashot {
using SettingsWait=std::function<DWORD(std::span<const HANDLE>)>;
// UI-thread query; the short-lived settings worker publishes its hook lifetime.
bool SettingsShortcutRecording();
bool EditPreferencesIsolated(HWND owner,Preferences& value,const std::function<bool(const Preferences&)>& accept,const SettingsWait& wait_events = {});
int SettingsWorkerMain(int count,wchar_t** arguments);
}
