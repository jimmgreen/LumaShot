#pragma once
#include "longshot/canvas.h"
#include "longshot/session.h"
#include "export/clipboard.h"
#include "longshot/annotations.h"
#include <optional>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace lumashot::longshot {
struct Host {
    std::function<bool()> dark;
    std::function<int()> clipboard_format;   // -1: bitmap only, otherwise paste-as-file format
    std::function<std::filesystem::path()> save_directory;
    std::function<void(const std::filesystem::path&)> remember_directory;
    // image: what the pin shows (annotations composited); ocr: clean pixels.
    std::function<void(Frame image, Frame ocr, POINT, bool recognize)> pin;
    std::function<bool()> ocr_available;
    std::function<void()> recapture;
    // Opens the screenshot editor over `bounds` (physical screen pixels) showing
    // `slice` with `marks` in slice coordinates. `done` receives the edited marks
    // (nullopt when canceled) and the follow-up the user picked in the editor:
    // 0 none, 9 save, 10 copy, 13 pin, 15 OCR. Returns false when the editor is busy.
    using AnnotateDone = std::function<void(std::optional<Document>, int follow)>;
    std::function<bool(std::shared_ptr<const Frame> slice, Document marks, RECT bounds, AnnotateDone done)> annotate;
};

// Independent window for one long image: tiled display, zoom, minimap
// navigation with crop handles, and copy / save / pin / OCR.
class Viewer {
public:
    Viewer(std::unique_ptr<CaptureResult> result, Host host, std::function<void(Viewer*)> closed);
    ~Viewer();
    Viewer(const Viewer&) = delete;
    Viewer& operator=(const Viewer&) = delete;
    HWND Window() const { return window_; }
    // Non-destructive annotations in long-image pixels.
    const std::vector<Mark>& Marks() const { return marks_.marks; }
    bool Annotating() const { return annotating_; }

private:
    struct Item { int id{}; Box box{}; };
    struct Layout { Box title, main, side, actions, minimap, strip; };
    struct Job { int kind{}; std::filesystem::path path; };
    struct JobResult { int kind{}; Frame frame, ocr; std::unique_ptr<ClipboardImage> clipboard; std::filesystem::path path; std::wstring error; };

    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Message(UINT, WPARAM, LPARAM);
    void Paint();
    void DrawImage(const Layout& l);
    void DrawSide(const Layout& l);
    void DrawActions(const Layout& l);
    void DrawTitle(const Layout& l);
    Layout Compute() const;
    std::vector<Item> Items() const;
    int Hit(Point p) const;
    void Action(int id);
    void Key(WPARAM key);
    void Wheel(short delta, POINT screen, bool control, bool shift);
    void ZoomAt(float zoom, Point anchor);
    void Clamp();
    float Zoom() const;
    float ContentLeft(const Layout& l) const;
    float ContentTop(const Layout& l) const;
    RECT Crop() const;  // effective columns/rows in image pixels
    float StripScale(const Layout& l) const;
    void Start(int kind, std::filesystem::path path = {});
    void Finished();
    void Toast(std::wstring text);
    void ResetTarget();
    void Invalidate() { InvalidateRect(window_, nullptr, FALSE); }
    void Annotate();
    void Annotated(std::optional<Document> edited, int follow, RECT slice, std::vector<size_t> taken);
    void MarksChanged();
    bool ConfirmDiscard();
    ComPtr<ID2D1Bitmap> CreateTile(int index, int level);

    Host host_;
    std::function<void(Viewer*)> closed_;
    std::shared_ptr<const Frame> image_;
    std::vector<int> seams_;
    int header_{}, footer_{}, scrollbar_{};
    RECT region_{};
    HWND window_{};
    float scale_{1};
    bool dark_{};
    Painter painter_;
    ComPtr<ID2D1HwndRenderTarget> target_;
    std::vector<ComPtr<ID2D1Bitmap>> tiles_;
    int tile_level_{-1};
    ComPtr<ID2D1Bitmap> overview_bitmap_;
    // View state (image pixels).
    bool fit_{true}, show_seams_{}, remove_scrollbar_{true}, trim_{};
    float zoom_{1}, scroll_x_{}, scroll_y_{};
    int crop_top_{}, crop_bottom_{};
    std::atomic<int> trim_left_{}, trim_right_{};  // written once by the overview worker
    int hover_{-1}, pressed_{-1};
    int drag_{};           // 1 pan, 2 minimap viewport, 3 crop top, 4 crop bottom
    POINT drag_start_{};
    float drag_x_{}, drag_y_{};
    std::wstring toast_;
    // Annotations.
    Document marks_;
    bool annotating_{}, marks_exported_{true};
    std::unique_ptr<Renderer> renderer_;  // tile composition, created on first use
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    // Background work.
    std::mutex mutex_;
    std::shared_ptr<Frame> overview_;
    std::atomic_bool busy_{};
    std::unique_ptr<JobResult> job_result_;
    std::jthread overview_worker_, job_worker_;
};
}
