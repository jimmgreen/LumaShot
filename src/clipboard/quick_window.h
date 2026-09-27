#pragma once
#include "clipboard/history.h"
#include "capture/frame.h"
#include <memory>

namespace lumashot::clipboard {
struct QuickRow { uint64_t id{}; Kind kind{}; std::wstring text; bool favorite{}; };
struct QuickCallbacks {
    std::function<void(size_t)> select;
    std::function<void(size_t)> paste;
    std::function<void(int)> category;
    std::function<void(int)> move;
    std::function<void()> close;
};
class QuickWindow {
public:
    explicit QuickWindow(QuickCallbacks callbacks);
    ~QuickWindow();
    QuickWindow(const QuickWindow&) = delete;
    QuickWindow& operator=(const QuickWindow&) = delete;
    void Show(HWND owner, POINT anchor, float scale, bool dark);
    void Update(std::vector<QuickRow> rows, size_t selected, int tab,
                bool continuous, bool busy, std::wstring status = {});
    void Hide();
    bool Visible() const;
    HWND Window() const;
    RECT Bounds() const;
    Frame Snapshot();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
