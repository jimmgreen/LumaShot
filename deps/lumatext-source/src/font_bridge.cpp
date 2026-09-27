#include "internal.hpp"

#include <dwrite_3.h>

lt_result lt::FontBridge::get_blob(IDWriteFontFace* face,
                                   std::shared_ptr<const FontBlob>& out) {
  if (!face) return LT_E_INVALID_ARGUMENT;
  {
    std::lock_guard lock(mutex_);
    auto found = cache_.find(face);
    if (found != cache_.end()) {
      out = found->second.blob;
      return LT_OK;
    }
  }

  UINT32 file_count = 0;
  HRESULT hr = face->GetFiles(&file_count, nullptr);
  if (FAILED(hr) || file_count != 1) return LT_E_FONT_UNAVAILABLE;

  ComPtr<IDWriteFontFile> file;
  IDWriteFontFile* file_ptr = nullptr;
  file_count = 1;
  hr = face->GetFiles(&file_count, &file_ptr);
  file.Attach(file_ptr);
  if (FAILED(hr) || !file) return LT_E_FONT_UNAVAILABLE;

  const void* reference_key = nullptr;
  UINT32 reference_key_size = 0;
  hr = file->GetReferenceKey(&reference_key, &reference_key_size);
  if (FAILED(hr)) return LT_E_FONT_UNAVAILABLE;

  ComPtr<IDWriteFontFileLoader> loader;
  hr = file->GetLoader(&loader);
  if (FAILED(hr)) return LT_E_FONT_UNAVAILABLE;

  ComPtr<IDWriteFontFileStream> stream;
  hr = loader->CreateStreamFromKey(reference_key, reference_key_size, &stream);
  if (FAILED(hr)) return LT_E_FONT_UNAVAILABLE;

  UINT64 file_size = 0;
  hr = stream->GetFileSize(&file_size);
  if (FAILED(hr) || file_size == 0 || file_size > static_cast<UINT64>(SIZE_MAX)) {
    return LT_E_FONT_UNAVAILABLE;
  }

  // Allocate the owner before acquiring a fragment. Any later allocation
  // failure releases the fragment, including loaders with a null context.
  auto fragment = std::make_shared<DWriteFragment>();
  const void* fragment_start = nullptr;
  hr = stream->ReadFileFragment(&fragment_start, 0, file_size,
                                &fragment->fragment_context);
  if (FAILED(hr)) return LT_E_FONT_UNAVAILABLE;
  fragment->stream = stream;
  if (!fragment_start) return LT_E_FONT_UNAVAILABLE;
  fragment->data = static_cast<const uint8_t*>(fragment_start);
  fragment->size = static_cast<size_t>(file_size);

  auto blob = std::make_shared<FontBlob>();
  blob->dwrite = std::move(fragment);
  blob->identity = next_font_identity();
  blob->face_index = face->GetIndex();
  ComPtr<IDWriteFontFace5> variable_face;
  if (SUCCEEDED(face->QueryInterface(IID_PPV_ARGS(&variable_face))) &&
      variable_face->HasVariations()) {
    const UINT32 axis_count = variable_face->GetFontAxisValueCount();
    std::vector<DWRITE_FONT_AXIS_VALUE> axis_values(axis_count);
    if (SUCCEEDED(variable_face->GetFontAxisValues(axis_values.data(), axis_count))) {
      blob->axes.reserve(axis_count);
      for (const auto& axis : axis_values) {
        blob->axes.push_back(FontBlob::AxisValue{
            static_cast<uint32_t>(axis.axisTag), axis.value});
      }
    }
  }

  std::lock_guard lock(mutex_);
  auto [position, inserted] = cache_.emplace(
      face, FaceEntry{ComPtr<IDWriteFontFace>(face), blob});
  out = inserted ? std::move(blob) : position->second.blob;
  return LT_OK;
}
