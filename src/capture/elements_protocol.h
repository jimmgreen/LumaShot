#pragma once
#include "capture/elements.h"
namespace lumashot::elements {
inline constexpr size_t MaxWindows = 32;
inline constexpr size_t MaxRegions = 1024;
struct Shared {
    DWORD window_count{};
    POINT pointer{};
    ElementWindow windows[MaxWindows]{};
    volatile LONG published{};
    ElementRegion regions[MaxRegions]{};
};
struct Handle {
    HANDLE value{};
    explicit Handle(HANDLE v = nullptr) : value(v) {}
    ~Handle() { if(value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
struct View {
    Shared* value{};
    explicit View(HANDLE mapping) : value(static_cast<Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)))) {}
    ~View() { if(value) UnmapViewOfFile(value); }
};
}
