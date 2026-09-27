#pragma once
#include "model/document.h"
namespace lumashot {
struct ArrowPath {std::vector<Point> points;bool filled{},closed{},shaft{};};
std::vector<ArrowPath> BuildArrow(const Mark& mark);
inline constexpr LPCWSTR ArrowTypeNames[]={L"标准箭头",L"双向箭头",L"弯曲箭头",L"手绘箭头",L"三角箭头",L"圆点起点"};
inline constexpr LPCWSTR ArrowHeadNames[]={L"实心三角",L"空心三角",L"圆角三角",L"菱形",L"圆形",L"短箭头",L"宽箭头",L"细长箭头",L"弧形箭头",L"斜切箭头",L"无箭头（线）"};
}
