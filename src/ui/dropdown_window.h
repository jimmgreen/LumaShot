#pragma once
#include "ui/dropdown_view.h"
#include <optional>
namespace lumashot::ui {
std::optional<int> TrackDropdown(HWND owner,Dropdown menu,bool dark);
}
