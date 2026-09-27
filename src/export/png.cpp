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

static void SaveImageFileImpl(const Frame&, const std::filesystem::path&, int, bool);
void SavePng(const Frame& frame, const std::filesystem::path& path) { SaveImageFile(frame,path,0); }
void SaveImageFile(const Frame& frame,const std::filesystem::path& path,int file_format) {
    SaveImageFileImpl(frame, path, file_format, false);
}
void SaveClipboardImageFile(const Frame& frame,const std::filesystem::path& path,int file_format) {
    SaveImageFileImpl(frame, path, file_format, true);
}
static void SaveImageFileImpl(const Frame& frame,const std::filesystem::path& path,int file_format,bool clipboard) {
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
    Check(encoded->SetSize(static_cast<UINT>(frame.Width()), static_cast<UINT>(frame.Height())));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    Check(encoded->SetPixelFormat(&format));
    if(format==GUID_WICPixelFormat32bppBGRA || format==GUID_WICPixelFormat32bppBGR)Check(encoded->WritePixels(static_cast<UINT>(frame.Height()),
        static_cast<UINT>(frame.Width()) * 4, static_cast<UINT>(frame.pixels.size() * 4),
        reinterpret_cast<BYTE*>(const_cast<uint32_t*>(frame.pixels.data()))));
    else if (format == GUID_WICPixelFormat24bppBGR) {
        // JPEG accepts packed BGR. Convert a bounded strip instead of asking WIC
        // to allocate a second full-size bitmap before encoding.
        constexpr UINT strip_rows = 32;
        const UINT stride = static_cast<UINT>(frame.Width()) * 3;
        std::vector<BYTE> strip(static_cast<size_t>(stride) * strip_rows);
        for (UINT y = 0; y < static_cast<UINT>(frame.Height()); y += strip_rows) {
            const UINT rows = std::min(strip_rows, static_cast<UINT>(frame.Height()) - y);
            const uint32_t* source = frame.pixels.data() + static_cast<size_t>(y) * frame.Width();
            for (size_t i = 0; i < static_cast<size_t>(rows) * frame.Width(); ++i) {
                strip[i * 3] = static_cast<BYTE>(source[i]);
                strip[i * 3 + 1] = static_cast<BYTE>(source[i] >> 8);
                strip[i * 3 + 2] = static_cast<BYTE>(source[i] >> 16);
            }
            Check(encoded->WritePixels(rows, stride, stride * rows, strip.data()));
        }
    }
    else {
        ComPtr<IWICBitmap> source;Check(factory->CreateBitmapFromMemory(frame.Width(),frame.Height(),GUID_WICPixelFormat32bppBGRA,frame.Width()*4,static_cast<UINT>(frame.pixels.size()*4),reinterpret_cast<BYTE*>(const_cast<uint32_t*>(frame.pixels.data())),&source));
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




