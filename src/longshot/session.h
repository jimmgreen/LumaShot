#pragma once
#include "capture/stitcher.h"
#include "longshot/canvas.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace lumashot::longshot {
enum class Mode { Auto, Manual };
enum class Speed { Slow, Normal, Fast };
enum class Phase { Starting, Running, Paused, Problem, Finishing };
enum class Problem { None, NoOverlap, LowMatch, NoScroll, Limit, Bottom, Error };

// What the UI shows; produced by the worker after every processed frame.
struct Snapshot {
    Phase phase{Phase::Starting};
    Mode mode{Mode::Auto};
    Speed speed{Speed::Normal};
    Problem problem{Problem::None};
    int width{}, height{}, frame_height{}, segments{}, header{}, footer{}, scrollbar{};
    double match{};
    bool can_undo{}, pending{};
    std::wstring message;
    // Bottom part of the downscaled result (panel preview) and where it sits.
    std::shared_ptr<const Frame> tail;
    int tail_first{}, thumb_height{};
    double thumb_scale{1};
    std::vector<int> seams;
};

struct CaptureResult {
    Frame image;               // full width, scrollbar included
    std::vector<int> seams;    // composed rows where segments start
    int header{}, footer{}, scrollbar{};
    RECT region{};
};

// One scrolling capture of a fixed screen region. The worker thread owns the
// stitcher and every desktop capture; the UI thread owns the windows.
class Session {
public:
    using Done = std::function<void(std::unique_ptr<CaptureResult>)>;
    using Closed = std::function<void()>;
    Session(RECT region, bool dark, Done done, Closed closed);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    RECT Region() const { return region_; }

private:
    struct Command { enum Kind { Pause, Resume, SetMode, SetSpeed, Undo, Force, Skip, Finish } kind{}; int value{}; };
    struct Item { int id{}; Box box{}; };

    // Worker side ----------------------------------------------------------
    void Worker();
    bool Wait(int milliseconds);          // false when cancel/finish interrupt
    bool Drain();                         // applies queued commands; false on cancel
    bool Running() const;
    void SyncThumb();
    void AutoStep();
    void ManualStep();
    std::optional<Frame> Settle(int first_delay, int limit);
    void Scroll(int notches, int direction);
    void Handle(const StitchResult& result, bool automatic, int notches);
    void Publish();
    void FinishWork();
    Frame Capture();
    HWND ScrollTarget() const;

    // UI side --------------------------------------------------------------
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK MouseHook(int, WPARAM, LPARAM);
    LRESULT Message(HWND, UINT, WPARAM, LPARAM);
    void Send(Command command);
    void Place();
    void PaintFrame();
    void PaintBar();
    void PaintPanel();
    void PaintAll();
    std::vector<Item> BarItems() const;
    std::vector<Item> PanelItems() const;
    Box PreviewBox() const;
    void Action(int id);
    void Key(WPARAM key);
    void Cancel();
    void Pointer(HWND window, UINT message, POINT client);

    RECT region_{}, work_{};
    bool dark_{}, acrylic_bar_{}, acrylic_panel_{};
    float scale_{1};
    Done done_;
    Closed closed_;
    HWND frame_{}, bar_{}, panel_{};
    RECT bar_rect_{}, panel_rect_{}, frame_rect_{};
    bool panel_inside_{};
    Painter painter_;
    LayeredCanvas frame_canvas_, bar_canvas_, panel_canvas_;
    Snapshot view_;
    int hover_{-1}, pressed_{-1};
    HWND hover_window_{};
    bool confirming_{}, finished_{};
    HHOOK hook_{};

    // Shared -----------------------------------------------------------------
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Command> commands_;
    bool cancel_{}, finish_{};
    Snapshot published_;
    std::unique_ptr<CaptureResult> result_;

    // Worker-only ------------------------------------------------------------
    std::unique_ptr<Stitcher> stitcher_;
    Mode mode_{Mode::Auto};
    Speed speed_{Speed::Normal};
    bool paused_{}, retry_{}, moving_{}, appended_once_{}, send_input_{};
    Problem problem_{Problem::None};
    std::wstring message_;
    int notches_{1}, unchanged_steps_{}, still_steps_{};
    double pixels_per_notch_{};
    // Wheel notches that produced each undoable segment (0 = not scrolled by
    // us), kept in step with the stitcher history so undo scrolls back right.
    std::deque<int> step_notches_;
    int pending_notches_{};           // notches of the held low-match segment
    void RecordSegment(int notches);
    RowSignatures last_sample_;
    int guard_{};
    std::atomic<uint64_t> input_tick_{};  // last user wheel/click in the region
    std::atomic<int> tail_rows_{400};     // preview height in pixels
    // Downscaled copy of the content rows for the live preview.
    int thumb_width_{};
    double thumb_scale_{1};
    std::vector<uint32_t> thumb_;
    std::vector<int> column_;
    int thumb_source_rows_{};
    int thumb_epoch_{-1};
    std::jthread worker_;
};
}
