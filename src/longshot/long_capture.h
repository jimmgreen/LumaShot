#pragma once
#include "longshot/viewer.h"
#include <memory>
#include <vector>

namespace lumashot {
// Owns the (single) running scroll-capture session and every open long-image
// viewer. Everything runs on the UI thread except the session/viewer workers.
class LongCaptureManager {
public:
    explicit LongCaptureManager(longshot::Host host);
    ~LongCaptureManager();
    LongCaptureManager(const LongCaptureManager&) = delete;
    LongCaptureManager& operator=(const LongCaptureManager&) = delete;
    // region: physical screen pixels, already clipped to one monitor.
    bool Start(RECT region);
    bool Active() const { return session_ != nullptr; }
    size_t Viewers() const { return viewers_.size(); }
private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    void Retire();
    void Collect();
    longshot::Host host_;
    HWND window_{};
    std::unique_ptr<longshot::Session> session_;
    std::vector<std::unique_ptr<longshot::Viewer>> viewers_;
    std::vector<std::unique_ptr<longshot::Session>> retired_sessions_;
    std::vector<std::unique_ptr<longshot::Viewer>> retired_viewers_;
};
}
