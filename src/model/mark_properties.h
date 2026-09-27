#pragma once
#include "model/tool_properties.h"
namespace lumashot {
// Mark dimensions are physical pixels; panel properties are DIP.
ToolProperties PropertiesOfMark(const Mark& mark,float dpi_scale,const ToolProperties& defaults);
Mark WithMarkProperties(const Mark& mark,const ToolProperties& properties,float dpi_scale);
}
