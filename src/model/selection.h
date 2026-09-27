#pragma once
#include "model/document.h"
namespace lumashot {
struct EditHandle {int id;Point point;};
Point RotatePoint(Point p,Point center,float degrees);
Point MarkCenter(const Mark& mark);
void ReconnectNumberBadge(Mark& mark);
Box PartBounds(const Mark&,EditPart);
std::vector<EditHandle> EditHandles(const Mark&,EditPart,float ui_scale);
int HitEditHandle(const Mark&,EditPart,Point,float ui_scale);
EditPart HitMarkPart(const Mark&,Point,float tolerance=6);
bool HitMark(const Mark&,Point,float tolerance=6);
// -1 moves, 0..7 resize, 8 rotates, 9..12 corner radii,
// 20..23 leader vertices, 24..25 arrow endpoints.
Mark EditMark(const Mark& original,EditPart part,int handle,Point start,Point current,bool proportional,bool snap,float ui_scale=1);
}
