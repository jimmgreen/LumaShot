#pragma once
#include <windows.h>
namespace lumashot {
inline unsigned PanelBackground(bool dark){return dark?0x252c38u:0xf7f9fcu;}
inline float PanelOpacity(bool dark,bool acrylic){return acrylic?(dark?.72f:.60f):1.f;}
// DWM owns backdrop sampling/blur. No desktop capture or repaint timer.
void ConfigureBorderlessWindow(HWND window);
void ResizeBorderlessWindow(HWND window,float scale,float radius=12);
bool SetSettingsAcrylic(HWND window,bool dark);
}


