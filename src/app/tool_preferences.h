#pragma once
#include "model/tool_properties.h"
#include <filesystem>
namespace lumashot {
ToolProperties LoadToolProperties(const std::filesystem::path& path,ToolProperties defaults={});
void SaveToolProperties(const std::filesystem::path& path,const ToolProperties& tools);
}
