#pragma once
#include "model/document.h"
#include <array>
namespace lumashot {
struct Hsv { float h{},s{},v{}; };
Hsv ToHsv(uint32_t color);
uint32_t FromHsv(Hsv hsv);
std::wstring ColorHex(uint32_t color);
bool ParseColorHex(std::wstring text,uint32_t& color);
struct ColorPicker {
    bool open{},replace{},invalid{};
    int drag{-1},field{-1};
    uint32_t color{},original{};
    Hsv hsv;
    Box bounds{};float scale{1};
    std::wstring input;
    std::vector<uint32_t> recent{0xffff4d4f,0xff008cff,0xff22c55e,0xffffaa16,0xff8844ff,0xff243142,0xff7c889b};
    void Open(uint32_t value,Box anchor,RECT monitor,float dpi_scale);
    Box Part(int id) const; // 0 square, 1 hue, 2 close, 3 HEX, 4..6 RGB, 7 preview, 10..16 recent, 17 add
    int Hit(Point point) const;
    void Set(uint32_t value);
    void Down(Point point);
    void Move(Point point);
    void Type(wchar_t ch);
    bool Commit();
    void Focus(int index);
    void Step(int index,int amount);
    void Remember();
};
}
