#pragma once
#include "model/document.h"
namespace lumashot {
struct ViewState;
namespace ui {
// Borrowed, process-cached handle. Do not call DestroyCursor on it.
HCURSOR RotationCursor(UINT dpi);
// UI-thread borrowed handle: use/copy immediately; the bounded cache protects the active cursor.
HCURSOR AngledResizeCursor(float degrees,UINT dpi);
HCURSOR SelectionEditCursor(const Document&,const ViewState&,Point,UINT dpi,int active_handle=-1);
}
}
