#pragma once
#include <d2d1.h>
#include <wrl/client.h>
#include <vector>

namespace lumashot {
// Native vector resources compiled from assets/icon.svg; no bitmap or SVG runtime.
class BrandIcon {
public:
    // trim_margin maps the visible artwork, rather than the SVG canvas, to bounds.
    void Draw(ID2D1RenderTarget* target,D2D1_RECT_F bounds,bool trim_margin=false);
    void Reset(){items_.clear();target_=nullptr;}
    explicit operator bool()const{return !items_.empty();}
private:
    struct Item {
        Microsoft::WRL::ComPtr<ID2D1Geometry> geometry;
        Microsoft::WRL::ComPtr<ID2D1Brush> fill,stroke;
        float stroke_width{};
    };
    std::vector<Item> items_;
    ID2D1RenderTarget* target_{};
    void Load(ID2D1RenderTarget* target);
};
}
