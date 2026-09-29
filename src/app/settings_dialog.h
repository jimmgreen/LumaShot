#include "ui/shortcut_label.h"
#pragma once
#include "app/preferences.h"
#include <functional>
#include <string>
namespace lumashot {
bool ShortcutModifier(UINT key);

bool ShowSettingsDialog(HWND owner,Preferences& value,const std::function<bool(const Preferences&)>& accept,const std::function<void(bool)>& recording_changed = {},const std::function<void()>& check_updates = {});
}