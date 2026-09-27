#pragma once
#include "model/document.h"
namespace lumashot::ui {
// Handle 0..7: rotate its original local cursor axis and preserve its visible dimensions.
HCURSOR ExactResizeHandleCursor(int handle,float rotation,UINT dpi);
}
