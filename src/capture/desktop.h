#pragma once
#include "capture/frame.h"

namespace lumashot {
Frame CaptureDesktop();
// Copies one physical-pixel rectangle of the desktop (used by long capture).
Frame CaptureRegion(RECT region);
}
