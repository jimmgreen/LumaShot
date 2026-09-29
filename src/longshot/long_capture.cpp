#include "longshot/long_capture.h"
#include "ui/themed_message.h"

namespace lumashot {
namespace {
constexpr UINT kCollect = WM_APP + 321;
constexpr wchar_t kClass[] = L"LumaShot.LongCaptureHost";
}

LongCaptureManager::LongCaptureManager(longshot::Host host) : host_(std::move(host)) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    RegisterClassW(&wc);
    // Message-only window: windows are never destroyed from inside their own
    // window procedure; retired objects are deleted from here instead.
    window_ = CreateWindowExW(0, kClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, this);
}

LongCaptureManager::~LongCaptureManager() {
    session_.reset();
    viewers_.clear();
    retired_sessions_.clear();
    retired_viewers_.clear();
    if (window_) {
        SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
        DestroyWindow(window_);
    }
}

bool LongCaptureManager::Start(RECT region) {
    if (session_ || region.right - region.left < 32 || region.bottom - region.top < 32) return false;
    const bool dark = host_.dark && host_.dark();
    session_ = std::make_unique<longshot::Session>(region, dark,
        [this](std::unique_ptr<longshot::CaptureResult> result) {
            Open(std::move(result));
            Retire();
        },
        [this] { Retire(); });
    return true;
}

bool LongCaptureManager::Open(std::unique_ptr<longshot::CaptureResult> result) {
    try {
        viewers_.push_back(std::make_unique<longshot::Viewer>(std::move(result), host_, [this](longshot::Viewer* viewer) {
            for (auto it = viewers_.begin(); it != viewers_.end(); ++it) {
                if (it->get() != viewer) continue;
                retired_viewers_.push_back(std::move(*it));
                viewers_.erase(it);
                break;
            }
            PostMessageW(window_, kCollect, 0, 0);
        }));
        return true;
    } catch (const std::exception&) {
        ui::ShowThemedMessage(nullptr, host_.dark && host_.dark(), L"长截图", L"无法打开长截图预览窗口，可能是内存不足。请缩小选区后重试。");
        return false;
    }
}

void LongCaptureManager::Retire() {
    if (session_) retired_sessions_.push_back(std::move(session_));
    PostMessageW(window_, kCollect, 0, 0);
}

void LongCaptureManager::Collect() {
    retired_sessions_.clear();
    retired_viewers_.clear();
}

LRESULT CALLBACK LongCaptureManager::Proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<LongCaptureManager*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<LongCaptureManager*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self && message == kCollect) {
        self->Collect();
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}
}
