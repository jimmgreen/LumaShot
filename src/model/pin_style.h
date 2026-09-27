#pragma once
namespace lumashot {
// Stable persisted values: effects decorate the desktop pin, never exported pixels.
enum class PinStyle : int { None, Simple, Rounded, Polaroid, Curl };
constexpr PinStyle DefaultPinStyle=PinStyle::Simple;
constexpr bool ValidPinStyle(int value){return value>=0&&value<=static_cast<int>(PinStyle::Curl);}
constexpr PinStyle NormalizePinStyle(int value){return ValidPinStyle(value)?static_cast<PinStyle>(value):DefaultPinStyle;}
}
