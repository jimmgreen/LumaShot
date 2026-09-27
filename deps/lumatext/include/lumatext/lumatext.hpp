#ifndef LUMATEXT_LUMATEXT_HPP
#define LUMATEXT_LUMATEXT_HPP

#include <lumatext/lumatext.h>
#include <utility>

namespace LumaText {

template <typename T>
class Handle {
 public:
  Handle() noexcept = default;
  explicit Handle(T* value) noexcept : value_(value) {}
  ~Handle() { reset(); }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
  Handle& operator=(Handle&& other) noexcept {
    if (this != &other) reset(std::exchange(other.value_, nullptr));
    return *this;
  }
  T* get() const noexcept { return value_; }
  T** put() noexcept { reset(); return &value_; }
  explicit operator bool() const noexcept { return value_ != nullptr; }
  void reset(T* value = nullptr) noexcept {
    if (value_) lt_release(value_);
    value_ = value;
  }

 private:
  T* value_ = nullptr;
};

using Context = Handle<lt_context>;
using Renderer = Handle<lt_renderer>;
using Frame = Handle<lt_frame>;
using Input = Handle<lt_input>;
using FontFace = Handle<lt_font_face>;
using FontCascade = Handle<lt_font_cascade>;
using TextLayout = Handle<lt_text_layout>;
using RenderProfile = Handle<lt_render_profile>;
using GlyphImage = Handle<lt_glyph_image>;

template <typename T>
T Descriptor() noexcept {
  T value{};
  value.struct_size = sizeof(T);
  value.abi_version = LT_ABI_VERSION;
  return value;
}

}  // namespace LumaText

#endif
