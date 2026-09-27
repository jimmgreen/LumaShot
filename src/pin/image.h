#pragma once
#include "ui/render.h"
namespace lumashot {
// Restore just the small patch beneath the sampled cursor before flattening
// annotations. This avoids retaining a second full-desktop screenshot.
Frame PinOcrImage(const Frame& frozen,const Frame* cursor_patch,const Document& document,RECT selection);
}
