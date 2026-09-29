#include "export/png.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <stdexcept>
#include <algorithm>

namespace lumashot {
using Microsoft::WRL::ComPtr;

static void Check(HRESULT result) {
    if (FAILED(result)) throw std::system_error(static_cast<int>(result),
        std::system_category(), "Windows imaging");
}

static ComPtr<IWICImagingFactory> Factory() {
    ComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)));
    return factory;
}

static void SaveImageFileImpl(const PixelView&, const std::filesystem::path&, int, bool);
void SavePng(const Frame& frame, const std::filesystem::path& path) { SaveImageFile(frame,path,0); }
void SaveImageFile(const Frame& frame,const std::filesystem::path& path,int file_format) {
    SaveImageFileImpl(PixelsOf(frame), path, file_format, false);
}
void SaveClipboardImageFile(const Frame& frame,const std::filesystem::path& path,int file_format) {
    SaveImageFileImpl(PixelsOf(frame), path, file_format, true);
}
void SaveImageFile(const PixelView& view,const std::filesystem::path& path,int file_format) {
    SaveImageFileImpl(view, path, file_format, false);
}
void SaveClipboardImageFile(const PixelView& view,const std::filesystem::path& path,int file_format) {
    SaveImageFileImpl(view, path, file_format, true);
}
static void SaveImageFileImpl(const PixelView& frame,const std::filesystem::path& path,int file_format,bool clipboard) {
    if(!frame.pixels||frame.width<=0||frame.height<=0||frame.stride<static_cast<size_t>(frame.width))throw std::invalid_argument("Invalid image");
    if(file_format<0||file_format>2)throw std::invalid_argument("Invalid image format");
    auto factory = Factory();
    ComPtr<IWICStream> stream;
    Check(factory->CreateStream(&stream));
    Check(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder;
    Check(factory->CreateEncoder(file_format==1?GUID_ContainerFormatJpeg:(file_format==2?GUID_ContainerFormatBmp:GUID_ContainerFormatPng), nullptr, &encoder));
    Check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> encoded;
    ComPtr<IPropertyBag2> options;
    Check(encoder->CreateNewFrame(&encoded, &options));
    if (clipboard && file_format == 0) {
        // Up is inexpensive and exploits repeated screenshot rows. Disk saves
        // retain the default WIC filtering.
        PROPBAG2 property{};
        property.pstrName = const_cast<LPOLESTR>(L"FilterOption");
        VARIANT value{};
        value.vt = VT_UI1;
        value.bVal = WICPngFilterUp;
        Check(options->Write(1, &property, &value));
    }
    Check(encoded->Initialize(options.Get()));
    const UINT width = static_cast<UINT>(frame.width), height = static_cast<UINT>(frame.height);
    Check(encoded->SetSize(width, height));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    Check(encoded->SetPixelFormat(&format));
    if ((format == GUID_WICPixelFormat32bppBGRA || format == GUID_WICPixelFormat32bppBGR) && frame.Contiguous()) {
        Check(encoded->WritePixels(height, width * 4, width * height * 4, reinterpret_cast<BYTE*>(const_cast<uint32_t*>(frame.pixels))));
    } else if (format == GUID_WICPixelFormat32bppBGRA || format == GUID_WICPixelFormat32bppBGR) {
        // A crop is packed band by band, so memory stays bounded.
        constexpr UINT band_rows = 256;
        std::vector<uint32_t> band(static_cast<size_t>(width) * std::min(band_rows, height));
        for (UINT y = 0; y < height; y += band_rows) {
            const UINT rows = std::min(band_rows, height - y);
            for (UINT r = 0; r < rows; ++r) std::copy_n(frame.Row(static_cast<int>(y + r)), width, band.data() + static_cast<size_t>(r) * width);
            Check(encoded->WritePixels(rows, width * 4, width * rows * 4, reinterpret_cast<BYTE*>(band.data())));
        }
    } else if (format == GUID_WICPixelFormat24bppBGR) {
        // JPEG accepts packed BGR. Convert a bounded strip instead of asking WIC
        // to allocate a second full-size bitmap before encoding.
        constexpr UINT strip_rows = 32;
        const UINT stride = width * 3;
        std::vector<BYTE> strip(static_cast<size_t>(stride) * strip_rows);
        for (UINT y = 0; y < height; y += strip_rows) {
            const UINT rows = std::min(strip_rows, height - y);
            for (UINT r = 0; r < rows; ++r) {
                const uint32_t* source = frame.Row(static_cast<int>(y + r));
                BYTE* out = strip.data() + static_cast<size_t>(r) * stride;
                for (UINT x = 0; x < width; ++x) {
                    out[x * 3] = static_cast<BYTE>(source[x]);
                    out[x * 3 + 1] = static_cast<BYTE>(source[x] >> 8);
                    out[x * 3 + 2] = static_cast<BYTE>(source[x] >> 16);
                }
            }
            Check(encoded->WritePixels(rows, stride, stride * rows, strip.data()));
        }
    }
    else {
        const UINT stride = static_cast<UINT>(frame.stride) * 4, bytes = stride * (height - 1) + width * 4;
        ComPtr<IWICBitmap> source;Check(factory->CreateBitmapFromMemory(width,height,GUID_WICPixelFormat32bppBGRA,stride,bytes,reinterpret_cast<BYTE*>(const_cast<uint32_t*>(frame.pixels)),&source));
        ComPtr<IWICFormatConverter> converter;Check(factory->CreateFormatConverter(&converter));
        Check(converter->Initialize(source.Get(),format,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));Check(encoded->WriteSource(converter.Get(),nullptr));
    }
    Check(encoded->Commit());
    Check(encoder->Commit());
}

Frame ReadPng(const std::filesystem::path& path) {
    auto factory = Factory();
    ComPtr<IWICBitmapDecoder> decoder;
    Check(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &decoder));
    ComPtr<IWICBitmapFrameDecode> decoded;
    Check(decoder->GetFrame(0, &decoded));
    UINT width{}, height{};
    Check(decoded->GetSize(&width, &height));
    Frame result = MakeFrame({0, 0, static_cast<LONG>(width), static_cast<LONG>(height)});
    ComPtr<IWICFormatConverter> converter;
    Check(factory->CreateFormatConverter(&converter));
    Check(converter->Initialize(decoded.Get(), GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
    Check(converter->CopyPixels(nullptr, width * 4,
        static_cast<UINT>(result.pixels.size() * 4), reinterpret_cast<BYTE*>(result.pixels.data())));
    return result;
}

}




