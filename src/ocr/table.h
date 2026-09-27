#pragma once
#include "ocr/text.h"
namespace lumashot::ocr {
std::optional<Table> AnalyzeTable(const Frame& image, const Text& text);
}
