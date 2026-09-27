#pragma once
#include "ocr/text.h"
#include <d2d1.h>
namespace lumashot {
enum class TextDecoration { Highlight, Wave, Underline, Strike };
struct SelectionMark {TextDecoration kind{};std::vector<Box> boxes;};
std::vector<Box> SelectionBoxes(const ocr::Text&,ocr::Selection);
struct SelectionHandles {Point start{},end{};bool visible{};};
SelectionHandles TextHandles(const ocr::Text&,ocr::Selection,float scale);
int HitTextHandle(SelectionHandles,Point,float scale);
struct SelectionBar {RECT bounds{};float scale{1};bool above{};};
SelectionBar PlaceSelectionBar(Box selected,RECT work,float scale);
void DrawTextMarks(ID2D1RenderTarget*,const std::vector<SelectionMark>&);
void DrawTextHandles(ID2D1RenderTarget*,SelectionHandles,float scale);
void DrawSelectionBar(ID2D1RenderTarget*,float width,float height,float scale,int hover,bool above=false,bool copied=false,int down=-1);
Frame FlattenTextMarks(const Frame&,const std::vector<SelectionMark>&);
}

