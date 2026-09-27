#include "capture/cursor.h"
#include "export/png.h"
#include <objbase.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

using namespace lumashot;
static int failures{};

static void Expect(bool condition, const char* name) {
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
    if (!condition) ++failures;
}

static size_t Changed(const Frame& frame, uint32_t original) {
    return static_cast<size_t>(std::count_if(frame.pixels.begin(), frame.pixels.end(),
        [original](uint32_t value) { return value != original; }));
}

static void CheckClippedReference(HCURSOR source) {
    // The old implementation also drew a CopyIcon snapshot. Drawing the shared
    // system handle directly can round translucent cursor channels differently.
    // Use an independent snapshot so this comparison isolates DIB size/stride.
    struct Icon {HICON value{};~Icon(){if(value)DestroyIcon(value);}} reference_icon{CopyIcon(source)};
    CheckWin32(reference_icon.value!=nullptr,"CopyIcon full-frame reference");
    const auto origin=FrozenCursor::Copy(source,{0,0}).Bounds();
    const LONG width=origin.right-origin.left,height=origin.bottom-origin.top;
    bool matched=true;
    for(const LONG x:{1-width,0L,19L,63L,64L})for(const LONG y:{1-height,0L,17L,47L,48L}) {
        auto frame=MakeFrame({-173,-91,-109,-43});
        for(size_t i=0;i<frame.pixels.size();++i)frame.pixels[i]=0xff000000|static_cast<uint32_t>((i*2654435761u)&0xffffff);
        auto expected=frame;
        auto frozen=FrozenCursor::Copy(source,{frame.bounds.left+x-origin.left,frame.bounds.top+y-origin.top});
        const auto bounds=frozen.Bounds();RECT intersection{};
        if(IntersectRect(&intersection,&bounds,&frame.bounds)) {
            DibSurface reference(frame.Width(),frame.Height());
            std::copy(expected.pixels.begin(),expected.pixels.end(),reference.Pixels());
            CheckWin32(DrawIconEx(reference.Dc(),bounds.left-frame.bounds.left,bounds.top-frame.bounds.top,
                reference_icon.value,width,height,0,nullptr,DI_NORMAL|DI_NOMIRROR)!=FALSE,"DrawIconEx full-frame reference");
            GdiFlush();
            std::copy_n(reference.Pixels(),expected.pixels.size(),expected.pixels.begin());
            for(auto& pixel:expected.pixels)pixel|=0xff000000;
        }
        frozen.Composite(frame);
        if(matched&&frame.pixels!=expected.pixels) {
            const auto mismatch=std::mismatch(frame.pixels.begin(),frame.pixels.end(),expected.pixels.begin());
            const auto index=static_cast<size_t>(mismatch.first-frame.pixels.begin());
            std::cout<<"First cursor mismatch: size="<<width<<"x"<<height<<" offset="<<x<<","<<y
                <<" pixel="<<(index%64)<<","<<(index/64)<<" actual="<<std::hex<<*mismatch.first
                <<" expected="<<*mismatch.second<<std::dec<<"\n";
        }
        matched=matched&&frame.pixels==expected.pixels;
    }
    Expect(matched,"small cursor surface equals full-frame reference at every edge/corner and outside");
}

static void CheckStandardCursors() {
    const std::array<LPCWSTR, 5> types{IDC_ARROW, IDC_HAND, IDC_IBEAM, IDC_SIZEWE, IDC_SIZENS};
    const std::array<const char*, 5> names{"arrow", "hand", "text", "horizontal resize", "vertical resize"};
    for (size_t i = 0; i < types.size(); ++i) {
        CheckClippedReference(LoadCursorW(nullptr,types[i]));
        Frame frame = MakeFrame({-240, -120, 16, 136}, 0xffabcdef);
        auto cursor = FrozenCursor::Copy(LoadCursorW(nullptr, types[i]), {-150, -30});
        const RECT bounds = cursor.Bounds();
        cursor.Composite(frame);
        Expect(Changed(frame, 0xffabcdef) > 0, names[i]);
        bool outside_unchanged = true;
        for (LONG y = frame.bounds.top; y < frame.bounds.bottom; ++y) {
            for (LONG x = frame.bounds.left; x < frame.bounds.right; ++x) {
                POINT p{x, y};
                if (!PtInRect(&bounds, p) && frame.pixels[static_cast<size_t>(y - frame.bounds.top) *
                    frame.Width() + x - frame.bounds.left] != 0xffabcdef) outside_unchanged = false;
            }
        }
        Expect(outside_unchanged, "cursor does not alter pixels outside its true bounds");
        auto clipped = Crop(frame, {-230, -110, -200, -80});
        Expect(Changed(clipped, 0xffabcdef) == 0, "selection excludes original cursor position");
    }
}

static void CheckMaskAndHotspot() {
    std::array<BYTE, 128> mask{};
    std::array<BYTE, 128> invert{};
    mask.fill(0xff);
    invert[0] = 0x80;
    HCURSOR source = CreateCursor(GetModuleHandleW(nullptr), 7, 9, 32, 32,
        mask.data(), invert.data());
    CheckWin32(source != nullptr, "CreateCursor test");
    CheckClippedReference(source);
    auto frozen = FrozenCursor::Copy(source, {-10, -20});
    DestroyCursor(source);
    Frame frame = MakeFrame({-30, -40, 34, 24}, 0xff123456);
    frozen.Composite(frame);
    Expect(frame.pixels[11 * 64 + 13] == 0xffedcba9,
        "monochrome XOR cursor preserves hotspot and negative desktop origin");
    Expect(Changed(frame, 0xff123456) == 1, "transparent cursor mask preserves background");
    Expect(frozen.Visible(), "cursor survives destruction of source handle");
    Frame edge = MakeFrame({-17, -29, -16, -28}, 0xff123456);
    frozen.Composite(edge);
    Expect(edge.pixels[0] == 0xffedcba9, "cursor clips correctly at a one-pixel selection");
    Frame outside = MakeFrame({100, 100, 132, 132}, 0xff123456);
    frozen.Composite(outside);
    Expect(Changed(outside, 0xff123456) == 0, "off-selection cursor is not moved into the capture");
    FrozenCursor hidden;
    hidden.Composite(outside);
    Expect(!hidden.Visible() && Changed(outside, 0xff123456) == 0, "hidden cursor is omitted");
}

static void CheckAlphaAndPng() {
    DibSurface color(32, 32);
    std::fill_n(color.Pixels(), 32 * 32, 0u);
    // CreateIconIndirect receives the source color; Windows creates the
    // cursor's alpha representation. Do not premultiply this source twice.
    color.Pixels()[0] = 0x80ff0000;
    HBITMAP bitmap = static_cast<HBITMAP>(GetCurrentObject(color.Dc(), OBJ_BITMAP));
    HBITMAP mask = CreateBitmap(32, 32, 1, 1, nullptr);
    ICONINFO info{};
    info.fIcon = FALSE;
    info.xHotspot = 3;
    info.yHotspot = 5;
    info.hbmMask = mask;
    info.hbmColor = bitmap;
    HICON source = CreateIconIndirect(&info);
    DeleteObject(mask);
    CheckWin32(source != nullptr, "CreateIconIndirect test");
    CheckClippedReference(source);
    auto frozen = FrozenCursor::Copy(source, {13, 15});
    DestroyIcon(source);
    Frame frame = MakeFrame({0, 0, 64, 64}, 0xff0000ff);
    frozen.Composite(frame);
    const uint32_t pixel = frame.pixels[10 * 64 + 10];
    std::cout << "Alpha cursor pixel: 0x" << std::hex << pixel << std::dec << '\n';
    Expect(((pixel >> 16) & 255) == 128 && (pixel & 255) >= 126 && (pixel & 255) <= 127,
        "32-bit alpha cursor blends into the captured background");
    Expect(std::all_of(frame.pixels.begin(), frame.pixels.end(),
        [](uint32_t p) { return (p >> 24) == 255; }), "export alpha remains opaque");
    const auto path = std::filesystem::temp_directory_path() /
        (L"LumaShot-cursor-test-" + std::to_wstring(GetCurrentProcessId()) + L".png");
    try {
        SavePng(frame, path);
        const auto decoded = ReadPng(path);
        Expect(decoded.pixels == frame.pixels, "saved PNG retains cursor pixels exactly");
    } catch (...) {
        std::filesystem::remove(path);
        throw;
    }
    std::filesystem::remove(path);
}

int main() {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 2;
    try {
        CheckStandardCursors();
        CheckMaskAndHotspot();
        CheckAlphaAndPng();
        bool rejected = false;
        try { (void)Crop(MakeFrame({0, 0, 5, 5}), {8, 8, 9, 9}); }
        catch (const std::invalid_argument&) { rejected = true; }
        Expect(rejected, "empty intersection is rejected");
        const DWORD before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        for (int i = 0; i < 200; ++i) {
            auto cursor = FrozenCursor::Copy(LoadCursorW(nullptr, IDC_HAND), {12, 12});
            auto frame = MakeFrame({0, 0, 64, 64});
            cursor.Composite(frame);
        }
        Expect(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == before,
            "200 cursor captures release all GDI objects");
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        ++failures;
    }
    CoUninitialize();
    std::cout << "Failures: " << failures << '\n';
    return failures ? 1 : 0;
}
