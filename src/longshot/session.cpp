#include "longshot/session.h"
#include "capture/desktop.h"
#include "ui/acrylic.h"
#include "ui/glass_surface.h"
#include "ui/themed_message.h"
#include <stdexcept>
#include <utility>
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <chrono>
#include <cmath>

#pragma comment(lib, "dwmapi.lib")

namespace lumashot::longshot {
namespace {
constexpr UINT kSnapshot = WM_APP + 301, kDone = WM_APP + 302, kUserInput = WM_APP + 303;
constexpr wchar_t kFrameClass[] = L"LumaShot.LongCaptureFrame";
constexpr wchar_t kBarClass[] = L"LumaShot.LongCaptureBar";
constexpr wchar_t kPanelClass[] = L"LumaShot.LongCapturePanel";
constexpr float kBarHeight = 52, kPanelWidth = 300;
constexpr int kMaxHeight = 30000;
Session* g_hooked{};

using Clock = std::chrono::steady_clock;
int Elapsed(Clock::time_point since) {
    return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - since).count());
}
double SpeedFraction(Speed speed) { return speed == Speed::Slow ? .35 : speed == Speed::Fast ? .75 : .55; }
const wchar_t* SpeedName(Speed speed) { return speed == Speed::Slow ? L"慢" : speed == Speed::Fast ? L"快" : L"中"; }
std::wstring Percent(double value) { return std::to_wstring(static_cast<int>(std::lround(value * 100))) + L"%"; }
// Rough PNG size of a screenshot: about a fifth of the raw BGRA bytes.
std::wstring EstimatePng(int width, int height) {
    const double mb = double(width) * height * 4 / 5 / (1024.0 * 1024.0);
    wchar_t text[32]{};
    swprintf_s(text, mb < 10 ? L"%.1f MB" : L"%.0f MB", mb);
    return text;
}
void Register(const wchar_t* name, WNDPROC proc, HCURSOR cursor) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = name;
    wc.hCursor = cursor;
    RegisterClassW(&wc); // Already registered by a previous session is fine.
}
}

Session::Session(RECT region, bool dark, Done done, Closed closed)
    : region_(region), dark_(dark), done_(std::move(done)), closed_(std::move(closed)) {
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromRect(&region_, MONITOR_DEFAULTTONEAREST), &monitor);
    work_ = monitor.rcWork;
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    Register(kFrameClass, Proc, LoadCursorW(nullptr, IDC_ARROW));
    Register(kBarClass, Proc, LoadCursorW(nullptr, IDC_ARROW));
    Register(kPanelClass, Proc, LoadCursorW(nullptr, IDC_ARROW));
    frame_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kFrameClass, L"", WS_POPUP, region_.left, region_.top, 1, 1, nullptr, nullptr, instance, this);
    bar_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kBarClass, L"LumaShot 长截图", WS_POPUP,
        region_.left, region_.bottom, 1, 1, nullptr, nullptr, instance, this);
    panel_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kPanelClass,
        L"LumaShot 长截图预览", WS_POPUP, region_.right, region_.top, 1, 1, nullptr, nullptr, instance, this);
    if (!frame_ || !bar_ || !panel_) {
        for (HWND w : {frame_, bar_, panel_}) if (w) DestroyWindow(w);
        throw std::runtime_error("Unable to create long capture windows");
    }
    scale_ = Scale(bar_);
    for (HWND w : {frame_, bar_, panel_}) SetWindowDisplayAffinity(w, WDA_EXCLUDEFROMCAPTURE);
    ConfigureBorderlessWindow(bar_);
    ConfigureBorderlessWindow(panel_);
    acrylic_bar_ = SetSettingsAcrylic(bar_, dark_);
    acrylic_panel_ = SetSettingsAcrylic(panel_, dark_);
    // Layered (UpdateLayeredWindow) windows ignore window regions, so the
    // outline is DWM's native rounded corner + border + shadow, which also
    // clips the acrylic. We paint the full rectangle and never a second,
    // differently rounded edge (that left acrylic slivers in the corners).

    const int width = region_.right - region_.left, height = region_.bottom - region_.top;
    guard_ = std::lround(24 * scale_);
    StitchOptions options;
    options.guard = guard_;
    options.max_height = kMaxHeight;
    options.min_overlap = std::max(16, static_cast<int>(std::lround(14 * scale_)));
    stitcher_ = std::make_unique<Stitcher>(options);
    thumb_width_ = std::max(1, std::min(width, static_cast<int>(std::lround((kPanelWidth - 28) * scale_))));
    thumb_scale_ = double(thumb_width_) / width;
    view_.width = width;
    view_.height = view_.frame_height = height;
    view_.message = L"正在准备…";
    Place();
    PaintAll();
    ShowWindow(frame_, SW_SHOWNOACTIVATE);
    ShowWindow(panel_, SW_SHOWNOACTIVATE);
    ShowWindow(bar_, SW_SHOW);
    SetForegroundWindow(bar_);
    SetFocus(bar_);
    g_hooked = this;
    hook_ = SetWindowsHookExW(WH_MOUSE_LL, MouseHook, instance, 0);
    worker_ = std::jthread([this] { Worker(); });
}

Session::~Session() {
    {
        std::lock_guard lock(mutex_);
        cancel_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
    if (hook_) UnhookWindowsHookEx(hook_);
    if (g_hooked == this) g_hooked = nullptr;
    for (HWND w : {frame_, bar_, panel_}) {
        if (!w) continue;
        SetWindowLongPtrW(w, GWLP_USERDATA, 0);
        DestroyWindow(w);
    }
}

// ---------------------------------------------------------------------------
// Worker
// ---------------------------------------------------------------------------
Frame Session::Capture() { return CaptureRegion(region_); }

bool Session::Wait(int milliseconds) {
    std::unique_lock lock(mutex_);
    return !wake_.wait_for(lock, std::chrono::milliseconds(milliseconds), [this] { return cancel_ || finish_; });
}

bool Session::Drain() {
    std::deque<Command> commands;
    {
        std::lock_guard lock(mutex_);
        if (cancel_) return false;
        commands.swap(commands_);
    }
    if (commands.empty()) return true;
    for (const auto& c : commands) {
        switch (c.kind) {
        case Command::Pause: paused_ = true; break;
        case Command::Resume:
            paused_ = false;
            if (problem_ != Problem::Error && problem_ != Problem::None) {
                stitcher_->DropPending();
                problem_ = Problem::None;
                message_.clear();
            }
            unchanged_steps_ = still_steps_ = 0;
            break;
        case Command::SetMode:
            mode_ = static_cast<Mode>(c.value);
            paused_ = false;
            if (problem_ != Problem::Error) problem_ = Problem::None;
            stitcher_->DropPending();
            unchanged_steps_ = still_steps_ = 0;
            message_ = mode_ == Mode::Manual ? L"手动模式：在选区里滚动页面，停下后自动拼接" : L"";
            last_sample_ = {};
            break;
        case Command::SetSpeed: speed_ = static_cast<Speed>(c.value); break;
        case Command::Undo:
            if (stitcher_->Undo()) {
                problem_ = Problem::None;
                if (mode_ == Mode::Auto && !step_notches_.empty()) {
                    // Scroll the page back to where the kept segment ended.
                    Scroll(step_notches_.back(), -1);
                    step_notches_.pop_back();
                    paused_ = true;
                    message_ = L"已撤回一段，按空格继续";
                } else {
                    message_ = L"已撤回一段";
                }
                last_sample_ = {};
            }
            break;
        case Command::Force:
            if (stitcher_->HasPending()) {
                const auto result = stitcher_->AcceptPending();
                problem_ = Problem::None;
                message_.clear();
                paused_ = false;
                if (result.status == StitchStatus::Limit) {
                    problem_ = Problem::Limit;
                    message_ = L"已达到 30,000 px 高度上限";
                    std::lock_guard lock(mutex_);
                    finish_ = true;
                }
            }
            break;
        case Command::Skip:
            stitcher_->DropPending();
            problem_ = Problem::None;
            message_.clear();
            paused_ = false;
            retry_ = true;
            break;
        case Command::Finish: break;
        }
    }
    Publish();
    return true;
}

bool Session::Running() const {
    if (paused_) return false;
    if (problem_ == Problem::None || problem_ == Problem::Bottom) return true;
    return mode_ == Mode::Manual && (problem_ == Problem::NoOverlap || problem_ == Problem::NoScroll);
}

void Session::Worker() {
    try {
        // Let DWM remove the frozen screenshot overlay before the first frame.
        Wait(160);
        {
            std::lock_guard lock(mutex_);
            if (cancel_) return;
        }
        DwmFlush();
        const Frame first = Capture();
        stitcher_->Add(first);
        last_sample_ = SignRows(first, guard_);
        message_.clear();
        Publish();
        for (;;) {
            if (!Drain()) return;
            bool finishing = false;
            {
                std::lock_guard lock(mutex_);
                finishing = finish_;
            }
            if (finishing) { FinishWork(); return; }
            if (Running()) {
                if (mode_ == Mode::Auto) AutoStep();
                else ManualStep();
                continue;
            }
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return cancel_ || finish_ || !commands_.empty(); });
        }
    } catch (const std::exception&) {
        problem_ = Problem::Error;
        message_ = L"截取屏幕失败，可以完成已拼接的部分或放弃。";
        paused_ = true;
        try { Publish(); } catch (...) {}
        std::unique_lock lock(mutex_);
        wake_.wait(lock, [this] { return cancel_ || finish_; });
        if (cancel_) return;
        lock.unlock();
        try {
            if (stitcher_ && stitcher_->Started()) FinishWork();
        } catch (...) {}
    }
}

std::optional<Frame> Session::Settle(int first_delay, int limit) {
    const auto start = Clock::now();
    if (!Wait(first_delay)) return std::nullopt;
    Frame previous = Capture();
    RowSignatures previous_rows = SignRows(previous, guard_);
    for (;;) {
        if (!Wait(55)) return std::nullopt;
        Frame current = Capture();
        RowSignatures rows = SignRows(current, guard_);
        // Smooth scrolling moves rows every frame; two equal samples mean the
        // animation (and most late repaints) has finished.
        if (SamePositionRatio(previous_rows, rows) >= .98 || Elapsed(start) > limit) return current;
        previous = std::move(current);
        previous_rows = std::move(rows);
    }
}

HWND Session::ScrollTarget() const {
    const POINT center{(region_.left + region_.right) / 2, (region_.top + region_.bottom) / 2};
    const DWORD self = GetCurrentProcessId();
    HWND window = WindowFromPoint(center);
    DWORD process = 0;
    if (window) GetWindowThreadProcessId(window, &process);
    if (window && process != self) return window;
    // Our own window is on top: walk the z-order below it.
    window = nullptr;
    for (HWND top = GetTopWindow(nullptr); top; top = GetWindow(top, GW_HWNDNEXT)) {
        if (!IsWindowVisible(top)) continue;
        GetWindowThreadProcessId(top, &process);
        if (process == self || (GetWindowLongPtrW(top, GWL_EXSTYLE) & WS_EX_TRANSPARENT)) continue;
        BOOL cloaked = FALSE;
        DwmGetWindowAttribute(top, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        RECT r{};
        if (cloaked || !GetWindowRect(top, &r) || !PtInRect(&r, center)) continue;
        window = top;
        break;
    }
    while (window) {
        POINT p = center;
        ScreenToClient(window, &p);
        HWND child = ChildWindowFromPointEx(window, p, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT);
        if (!child || child == window) break;
        window = child;
    }
    return window;
}

void Session::Scroll(int notches, int direction) {
    const POINT center{(region_.left + region_.right) / 2, (region_.top + region_.bottom) / 2};
    const short delta = static_cast<short>(-WHEEL_DELTA * direction);
    if (!send_input_) {
        if (HWND target = ScrollTarget()) {
            for (int i = 0; i < notches; ++i) {
                PostMessageW(target, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(delta)),
                    MAKELPARAM(static_cast<WORD>(static_cast<short>(center.x)), static_cast<WORD>(static_cast<short>(center.y))));
                if (i + 1 < notches && !Wait(12)) return;
            }
            return;
        }
        send_input_ = true;
    }
    // Some windows ignore posted wheel messages. Inject real wheel input at
    // the region centre, then put the pointer back.
    POINT saved{};
    GetCursorPos(&saved);
    SetCursorPos(center.x, center.y);
    for (int i = 0; i < notches; ++i) {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        input.mi.mouseData = static_cast<DWORD>(static_cast<LONG>(delta));
        SendInput(1, &input, sizeof(input));
        Sleep(18);
    }
    Sleep(30);
    SetCursorPos(saved.x, saved.y);
}

void Session::AutoStep() {
    const int notches = notches_;
    const int hint = pixels_per_notch_ > 0 ? static_cast<int>(std::lround(pixels_per_notch_ * notches)) : 0;
    if (retry_) retry_ = false;
    else Scroll(notches, 1);
    auto frame = Settle(90, 1200);
    if (!frame) return;
    StitchResult result = stitcher_->Add(*frame, hint);
    if (result.status == StitchStatus::Unchanged) {
        // Lazy-loaded pages often need a moment before more content appears.
        if (!Wait(320)) return;
        result = stitcher_->Add(Capture(), hint);
    }
    if (result.status == StitchStatus::Unchanged) {
        if (!appended_once_) {
            if (++still_steps_ >= 2) {
                still_steps_ = 0;
                if (!send_input_) send_input_ = true;
                else {
                    mode_ = Mode::Manual;
                    problem_ = Problem::NoScroll;
                    message_ = L"这个窗口不响应自动滚动，已切到手动模式：请在选区里滚动滚轮。";
                    last_sample_ = {};
                }
            }
        } else if (++unchanged_steps_ >= 2) {
            problem_ = Problem::Bottom;
            message_ = L"已到达页面底部";
            std::lock_guard lock(mutex_);
            finish_ = true;
        }
        Publish();
        return;
    }
    unchanged_steps_ = still_steps_ = 0;
    Handle(result, true, notches);
}

void Session::ManualStep() {
    const bool recent = moving_ || GetTickCount64() - input_tick_.load() < 1500;
    {
        std::unique_lock lock(mutex_);
        if (wake_.wait_for(lock, std::chrono::milliseconds(recent ? 110 : 450),
                [this] { return cancel_ || finish_ || !commands_.empty(); }))
            return;
    }
    Frame current = Capture();
    RowSignatures rows = SignRows(current, guard_);
    const bool stable = !last_sample_.hash.empty() && SamePositionRatio(last_sample_, rows) >= .98;
    last_sample_ = std::move(rows);
    if (!stable) {
        moving_ = true;
        return;
    }
    if (!moving_) return;
    moving_ = false;
    const StitchResult result = stitcher_->Add(current);
    if (result.status == StitchStatus::Unchanged) return;
    Handle(result, false, 0);
}

void Session::Handle(const StitchResult& result, bool automatic, int notches) {
    switch (result.status) {
    case StitchStatus::Appended:
    case StitchStatus::Limit: {
        problem_ = Problem::None;
        message_.clear();
        appended_once_ = true;
        if (automatic && notches > 0) {
            step_notches_.push_back(notches);
            if (step_notches_.size() > 64) step_notches_.erase(step_notches_.begin());
            const double per = double(result.shift) / notches;
            pixels_per_notch_ = pixels_per_notch_ > 0 ? .5 * pixels_per_notch_ + .5 * per : per;
            const double band = std::max(1, stitcher_->BandRows());
            notches_ = std::clamp(static_cast<int>(std::lround(SpeedFraction(speed_) * band / pixels_per_notch_)), 1, 12);
            while (notches_ > 1 && notches_ * pixels_per_notch_ > .8 * band) --notches_;
        }
        if (result.status == StitchStatus::Limit) {
            problem_ = Problem::Limit;
            message_ = L"已达到 30,000 px 高度上限";
            std::lock_guard lock(mutex_);
            finish_ = true;
        }
        break;
    }
    case StitchStatus::NoOverlap:
        if (automatic && notches > 1) {
            // Scrolled past the viewport: go back and continue with smaller steps.
            Scroll(notches, -1);
            notches_ = std::max(1, notches / 2);
            pixels_per_notch_ = 0;
            Settle(90, 1200);
            break;
        }
        problem_ = Problem::NoOverlap;
        message_ = automatic ? L"这一步滚动太多，和上一段接不上。可以撤回后继续，或直接完成。"
                             : L"滚得太快，和上一段接不上：请往回滚一点。";
        break;
    case StitchStatus::LowMatch:
        problem_ = Problem::LowMatch;
        message_ = L"这一段和上一段的重叠部分差异较大（相似度 " + Percent(result.match) + L"），可能有动画或内容还在加载。";
        break;
    case StitchStatus::Mismatch:
        problem_ = Problem::Error;
        message_ = L"选区尺寸发生变化，无法继续拼接。";
        break;
    default: break;
    }
    Publish();
}

void Session::SyncThumb() {
    const Stitcher& s = *stitcher_;
    const int width = s.Width();
    if (width <= 0) return;
    if (column_.size() != static_cast<size_t>(width)) {
        column_.resize(static_cast<size_t>(width));
        for (int x = 0; x < width; ++x) column_[x] = std::min(thumb_width_ - 1, static_cast<int>(int64_t(x) * thumb_width_ / width));
    }
    const int content = s.Height() - s.FooterRows();
    if (content < thumb_source_rows_) {
        thumb_.clear();
        thumb_source_rows_ = 0;
    }
    const int built = static_cast<int>(thumb_.size() / thumb_width_);
    const int target = static_cast<int>(std::floor(content * thumb_scale_));
    std::vector<uint32_t> sum(static_cast<size_t>(thumb_width_) * 3), count(static_cast<size_t>(thumb_width_));
    for (int oy = built; oy < target; ++oy) {
        const int y0 = static_cast<int>(oy / thumb_scale_);
        const int y1 = std::min(content, std::max(y0 + 1, static_cast<int>((oy + 1) / thumb_scale_)));
        std::fill(sum.begin(), sum.end(), 0u);
        std::fill(count.begin(), count.end(), 0u);
        for (int y = y0; y < y1; ++y) {
            const uint32_t* row = s.Row(y);
            for (int x = 0; x < width; ++x) {
                const uint32_t p = row[x];
                uint32_t* t = &sum[static_cast<size_t>(column_[x]) * 3];
                t[0] += p & 255; t[1] += (p >> 8) & 255; t[2] += (p >> 16) & 255;
                ++count[column_[x]];
            }
        }
        for (int x = 0; x < thumb_width_; ++x) {
            const uint32_t n = std::max(1u, count[x]);
            const uint32_t* t = &sum[static_cast<size_t>(x) * 3];
            thumb_.push_back(0xff000000u | ((t[2] / n) << 16) | ((t[1] / n) << 8) | (t[0] / n));
        }
    }
    thumb_source_rows_ = content;
}

void Session::Publish() {
    Snapshot s;
    const Stitcher& st = *stitcher_;
    bool finishing = false;
    {
        std::lock_guard lock(mutex_);
        finishing = finish_;
    }
    s.mode = mode_;
    s.speed = speed_;
    s.problem = problem_;
    s.phase = finishing ? Phase::Finishing : !st.Started() ? Phase::Starting
        : (problem_ != Problem::None && problem_ != Problem::Bottom && !Running()) ? Phase::Problem
        : paused_ ? Phase::Paused : Phase::Running;
    s.width = st.Width();
    s.height = st.Height();
    s.frame_height = st.FrameHeight();
    s.segments = st.Segments();
    s.header = st.HeaderRows();
    s.footer = st.FooterRows();
    s.scrollbar = st.ScrollbarColumns();
    s.can_undo = st.CanUndo();
    s.pending = st.HasPending();
    s.message = message_;
    s.seams = st.Seams();
    s.thumb_scale = thumb_scale_;
    if (st.Started()) {
        SyncThumb();
        const int content_rows = static_cast<int>(thumb_.size() / thumb_width_);
        // The fixed footer comes from the newest frame; reduce it separately.
        const int footer_source = st.Height() - thumb_source_rows_;
        const int footer_rows = footer_source > 0 ? std::max(1, static_cast<int>(std::lround(footer_source * thumb_scale_))) : 0;
        const int total = content_rows + footer_rows;
        const int tail = std::min(total, std::max(1, tail_rows_.load()));
        const int first = total - tail;
        auto frame = std::make_shared<Frame>(MakeFrame({0, 0, thumb_width_, std::max(1, tail)}));
        for (int y = first; y < total; ++y) {
            uint32_t* out = frame->pixels.data() + static_cast<size_t>(y - first) * thumb_width_;
            if (y < content_rows) {
                std::copy_n(thumb_.data() + static_cast<size_t>(y) * thumb_width_, thumb_width_, out);
            } else {
                const int sy = thumb_source_rows_ + std::min(footer_source - 1,
                    static_cast<int>((y - content_rows) * double(footer_source) / std::max(1, footer_rows)));
                const uint32_t* row = st.Row(sy);
                for (int x = 0; x < thumb_width_; ++x) out[x] = row[std::min(st.Width() - 1, static_cast<int>(x / thumb_scale_))];
            }
        }
        s.tail = std::move(frame);
        s.tail_first = first;
        s.thumb_height = total;
    }
    {
        std::lock_guard lock(mutex_);
        published_ = std::move(s);
    }
    PostMessageW(bar_, kSnapshot, 0, 0);
}

void Session::FinishWork() {
    // Pick up whatever scrolled in since the last processed frame.
    try {
        const auto result = stitcher_->Add(Capture());
        if (result.status == StitchStatus::LowMatch) stitcher_->DropPending();
    } catch (...) {}
    Publish();
    auto result = std::make_unique<CaptureResult>();
    result->image = stitcher_->Compose(false);
    result->seams = stitcher_->Seams();
    result->header = stitcher_->HeaderRows();
    result->footer = stitcher_->FooterRows();
    result->scrollbar = stitcher_->ScrollbarColumns();
    result->region = region_;
    stitcher_.reset(); // Release the working copy before the viewer allocates.
    {
        std::lock_guard lock(mutex_);
        if (cancel_) return;
        result_ = std::move(result);
    }
    PostMessageW(bar_, kDone, 0, 0);
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------
void Session::Send(Command command) {
    {
        std::lock_guard lock(mutex_);
        if (command.kind == Command::Finish) finish_ = true;
        else commands_.push_back(command);
    }
    wake_.notify_all();
}

void Session::Place() {
    const float s = scale_;
    const auto items = BarItems();
    const int bar_w = static_cast<int>(std::lround((items.empty() ? 400.f : items.back().box.right / s + 12) * s));
    const int bar_h = static_cast<int>(std::lround(kBarHeight * s));
    const int gap = static_cast<int>(std::lround(10 * s)), margin = static_cast<int>(std::lround(8 * s));
    int x = (region_.left + region_.right - bar_w) / 2;
    x = std::clamp(x, static_cast<int>(work_.left) + margin, std::max(static_cast<int>(work_.left) + margin, static_cast<int>(work_.right) - margin - bar_w));
    int y = region_.bottom + gap;
    if (y + bar_h > work_.bottom - margin) {
        y = region_.top - gap - bar_h;
        if (y < work_.top + margin) y = region_.bottom - gap - bar_h; // inside, excluded from capture
    }
    bar_rect_ = {x, y, x + bar_w, y + bar_h};

    const int panel_w = static_cast<int>(std::lround(kPanelWidth * s));
    const int work_h = work_.bottom - work_.top - 2 * margin;
    const int panel_h = std::min(work_h, std::max(static_cast<int>(std::lround(360 * s)),
        std::min(static_cast<int>(region_.bottom - region_.top), static_cast<int>(std::lround(600 * s)))));
    int px = region_.right + static_cast<int>(std::lround(12 * s));
    panel_inside_ = false;
    if (px + panel_w > work_.right - margin) {
        px = region_.left - static_cast<int>(std::lround(12 * s)) - panel_w;
        if (px < work_.left + margin) {
            px = region_.right - panel_w - static_cast<int>(std::lround(12 * s));
            panel_inside_ = true;
        }
    }
    int py = std::clamp(static_cast<int>(region_.top), static_cast<int>(work_.top) + margin,
        std::max(static_cast<int>(work_.top) + margin, static_cast<int>(work_.bottom) - margin - panel_h));
    panel_rect_ = {px, py, px + panel_w, py + panel_h};
    // The bar and the panel must never overlap (a short region puts the bar
    // right where a tall panel hangs down). Prefer a shorter panel, then slide
    // the bar sideways, and finally move it above the region.
    {
        RECT hit{};
        const int min_panel = static_cast<int>(std::lround(300 * s));
        if (IntersectRect(&hit, &bar_rect_, &panel_rect_) && bar_rect_.top > panel_rect_.top && bar_rect_.top - gap - panel_rect_.top >= min_panel)
            panel_rect_.bottom = bar_rect_.top - gap;
        if (IntersectRect(&hit, &bar_rect_, &panel_rect_)) {
            const int left_x = static_cast<int>(panel_rect_.left) - gap - bar_w, right_x = static_cast<int>(panel_rect_.right) + gap;
            const int lo = static_cast<int>(work_.left) + margin, hi = static_cast<int>(work_.right) - margin - bar_w;
            const bool panel_right = (panel_rect_.left + panel_rect_.right) / 2 >= (bar_rect_.left + bar_rect_.right) / 2;
            int nx = panel_right ? left_x : right_x;
            if (nx < lo || nx > hi) nx = panel_right ? right_x : left_x;
            if (nx >= lo && nx <= hi) OffsetRect(&bar_rect_, nx - bar_rect_.left, 0);
        }
        if (IntersectRect(&hit, &bar_rect_, &panel_rect_)) {
            const int above = static_cast<int>(region_.top) - gap - bar_h;
            if (above >= work_.top + margin) OffsetRect(&bar_rect_, 0, above - bar_rect_.top);
        }
    }
    tail_rows_ = std::max(1, static_cast<int>(PreviewBox().bottom - PreviewBox().top));

    const int pad = static_cast<int>(std::lround(6 * s));
    frame_rect_ = {region_.left - pad, region_.top - pad, region_.right + pad, region_.bottom + pad};
    SetWindowPos(frame_, HWND_TOPMOST, frame_rect_.left, frame_rect_.top, frame_rect_.right - frame_rect_.left,
        frame_rect_.bottom - frame_rect_.top, SWP_NOACTIVATE);
    SetWindowPos(bar_, HWND_TOPMOST, bar_rect_.left, bar_rect_.top, bar_w, bar_h, SWP_NOACTIVATE);
    SetWindowPos(panel_, HWND_TOPMOST, panel_rect_.left, panel_rect_.top, panel_w, panel_rect_.bottom - panel_rect_.top, SWP_NOACTIVATE);
}

std::vector<Session::Item> Session::BarItems() const {
    const float s = scale_;
    std::vector<Item> items;
    float x = 12;
    const auto add = [&](int id, float width) {
        items.push_back({id, {x * s, 10 * s, (x + width) * s, 42 * s}});
        x += width;
    };
    add(100, 158);  x += 17;     // status (drag area)
    add(1, 104);    add(2, 104); x += 12;  // auto | manual: icon + 4 CJK glyphs with padding
    add(3, 100);    x += 17;     // speed
    add(4, 34);     x += 2;      add(5, 34); x += 17; // pause, undo
    add(6, 34);     x += 6;      add(7, 72);          // cancel, done
    return items;
}

Box Session::PreviewBox() const {
    const float s = scale_;
    const float w = static_cast<float>(panel_rect_.right - panel_rect_.left);
    const float h = static_cast<float>(panel_rect_.bottom - panel_rect_.top);
    float bottom = h - 14 * s - 50 * s;       // stats
    bottom -= 2 * 28 * s;                      // chips (reserved so the layout never jumps)
    if (view_.problem != Problem::None && view_.problem != Problem::Bottom) bottom -= 118 * s;
    return {14 * s, 46 * s, w - 14 * s, std::max(90 * s, bottom - 8 * s)};
}

std::vector<Session::Item> Session::PanelItems() const {
    std::vector<Item> items;
    if (view_.problem == Problem::None || view_.problem == Problem::Bottom) return items;
    const float s = scale_;
    const Box preview = PreviewBox();
    const float top = preview.bottom + 8 * s;
    const float y0 = top + 72 * s, y1 = y0 + 30 * s;
    float x = preview.left + 12 * s;
    const auto add = [&](int id, float width) { items.push_back({id, {x, y0, x + width * s, y1}}); x += (width + 6) * s; };
    switch (view_.problem) {
    case Problem::LowMatch: add(20, 84); add(21, 60); add(5, 60); break;
    case Problem::NoOverlap:
        if (view_.mode == Mode::Auto) { add(5, 60); add(22, 60); }
        add(7, 60);
        break;
    case Problem::Error: if (view_.segments > 0) add(7, 60); add(6, 60); break;
    default: break;
    }
    return items;
}

void Session::PaintAll() {
    PaintFrame();
    PaintBar();
    PaintPanel();
}

void Session::PaintFrame() {
    RECT screen{};
    GetWindowRect(frame_, &screen);
    frame_canvas_.Paint(frame_, painter_, screen, scale_, dark_, [&] {
        const float s = scale_;
        const uint32_t color = view_.phase == Phase::Problem ? 0xffef4444
            : view_.phase == Phase::Paused ? 0xfff59e0b : 0xff2f8cff;
        const float pad = static_cast<float>(region_.left - screen.left);
        const Box inner{pad, pad, static_cast<float>(screen.right - screen.left) - pad, static_cast<float>(screen.bottom - screen.top) - pad};
        // Soft halo, then the crisp ring just outside the captured pixels.
        painter_.Stroke({inner.left - 3 * s, inner.top - 3 * s, inner.right + 3 * s, inner.bottom + 3 * s}, (color & 0xffffff) | 0x38000000, 4 * s, 3 * s);
        painter_.Stroke({inner.left - 1 * s, inner.top - 1 * s, inner.right + 1 * s, inner.bottom + 1 * s}, color, 2 * s, 2 * s);
        const float arm = std::min(22 * s, (inner.right - inner.left) / 4);
        const float o = 2.5f * s, w = 3.5f * s;
        for (int corner = 0; corner < 4; ++corner) {
            const float x = corner % 2 ? inner.right + o : inner.left - o;
            const float y = corner / 2 ? inner.bottom + o : inner.top - o;
            const float dx = corner % 2 ? -arm : arm, dy = corner / 2 ? -arm : arm;
            painter_.Line({x, y}, {x + dx, y}, color, w);
            painter_.Line({x, y}, {x, y + dy}, color, w);
        }
    });
}

void Session::PaintBar() {
    RECT screen{};
    GetWindowRect(bar_, &screen);
    screen.right = screen.left + (bar_rect_.right - bar_rect_.left);
    screen.bottom = screen.top + (bar_rect_.bottom - bar_rect_.top);
    bar_canvas_.Paint(bar_, painter_, screen, scale_, dark_, [&] {
        const float s = scale_;
        const auto theme = painter_.Theme();
        const Box all{0, 0, static_cast<float>(screen.right - screen.left), static_cast<float>(screen.bottom - screen.top)};
        painter_.Backdrop(all, acrylic_bar_);
        const auto items = BarItems();
        const auto find = [&](int id) { for (const auto& i : items) if (i.id == id) return i.box; return Box{}; };
        // Status
        const Box status = find(100);
        const uint32_t dot = view_.phase == Phase::Problem ? 0xffef4444 : view_.phase == Phase::Paused ? 0xfff59e0b
            : view_.phase == Phase::Finishing ? 0xff2f8cff : view_.phase == Phase::Starting ? theme.Muted() : 0xff22c55e;
        const float cy = (status.top + status.bottom) / 2;
        painter_.Target()->FillEllipse(D2D1::Ellipse({status.left + 8 * s, cy}, 4.5f * s, 4.5f * s), painter_.Brush(dot));
        painter_.Target()->DrawEllipse(D2D1::Ellipse({status.left + 8 * s, cy}, 7.5f * s, 7.5f * s), painter_.Brush((dot & 0xffffff) | 0x40000000), 1.5f * s);
        const wchar_t* title = view_.phase == Phase::Starting ? L"准备中" : view_.phase == Phase::Paused ? L"已暂停"
            : view_.phase == Phase::Problem ? L"需要处理" : view_.phase == Phase::Finishing ? L"正在生成"
            : view_.mode == Mode::Manual ? L"手动拼接中" : L"拼接中";
        painter_.Text(title, {status.left + 22 * s, status.top - 1 * s, status.right, status.top + 18 * s}, 13, theme.Ink(), true);
        std::wstring sub = Thousands(view_.segments) + L" 段 · " + Thousands(view_.height) + L" px";
        if (view_.phase == Phase::Starting) sub = L"正在截取第一屏";
        painter_.Text(sub, {status.left + 22 * s, status.top + 17 * s, status.right, status.bottom + 2 * s}, 11.5f, theme.Muted());
        // Separators
        for (int id : {1, 4, 6}) {
            const Box b = find(id);
            const float x = b.left - 8.5f * s;
            painter_.Line({x, 16 * s}, {x, 36 * s}, theme.Border(), s);
        }
        // Segmented auto/manual
        const Box a = find(1), m = find(2);
        const Box group{a.left, a.top, m.right, m.bottom};
        painter_.Fill(group, dark_ ? 0x2a0b1220 : 0x40ffffff, 8 * s);
        painter_.Stroke(group, theme.Border(), 8 * s, s);
        const bool manual = view_.mode == Mode::Manual;
        const Box pill = manual ? m : a;
        const Box inset{pill.left + 3 * s, pill.top + 3 * s, pill.right - 3 * s, pill.bottom - 3 * s};
        painter_.Fill(inset, dark_ ? 0xff2c3a52 : 0xffffffff, 6 * s);
        painter_.Stroke(inset, dark_ ? 0x40ffffff : 0x1f0f2640, 6 * s, s);
        for (int id : {1, 2}) {
            const Box b = find(id);
            const bool on = (id == 2) == manual;
            const bool hovered = hover_window_ == bar_ && hover_ == id;
            if (!on && hovered) painter_.Fill({b.left + 3 * s, b.top + 3 * s, b.right - 3 * s, b.bottom - 3 * s}, dark_ ? 0x1effffff : 0x140f2640, 6 * s);
            const uint32_t ink = on ? theme.Accent() : (hovered ? theme.Ink() : theme.Muted());
            const wchar_t* label = id == 1 ? L"自动滚动" : L"手动滚动";
            // Centre icon + label inside the pill; measured bold so the layout
            // does not shift when the selection changes.
            const float icon = 16 * s, gap = 5 * s;
            const float text_w = std::ceil(painter_.Measure(label, 12, true).x);
            const float left = std::round((b.left + b.right - icon - gap - text_w) / 2);
            const float mid = (b.top + b.bottom) / 2;
            painter_.Icon(id == 1 ? Glyph::Auto : Glyph::Manual, {left, mid - icon / 2, left + icon, mid + icon / 2}, ink);
            painter_.Text(label, {left + icon + gap, b.top, b.right - 6 * s, b.bottom}, 12, ink, on,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        // Buttons through the shared control renderer.
        std::vector<ui::Control> controls;
        const auto button = [&](int id, int glyph, std::wstring text, bool enabled, bool primary = false, ui::Kind kind = ui::Kind::Button) {
            ui::Control c;
            c.id = id;
            c.kind = kind;
            c.bounds = find(id);
            c.icon = glyph;
            c.text = std::move(text);
            c.enabled = enabled;
            c.primary = primary;
            controls.push_back(std::move(c));
        };
        const bool live = view_.phase != Phase::Finishing && view_.phase != Phase::Starting;
        button(3, static_cast<int>(Glyph::Speed), std::wstring(L"速度 ") + SpeedName(view_.speed), live && !manual, false, ui::Kind::Dropdown);
        const bool running = view_.phase == Phase::Running;
        button(4, static_cast<int>(running ? Glyph::Pause : Glyph::Play), L"", live && view_.phase != Phase::Problem);
        button(5, static_cast<int>(Glyph::Undo), L"", live && view_.can_undo);
        button(6, static_cast<int>(Glyph::Close), L"", view_.phase != Phase::Finishing);
        button(7, -1, L"完成", view_.phase != Phase::Finishing && view_.segments > 0, true);
        auto paint = painter_.Context(acrylic_bar_);
        ui::DrawControls(paint, theme, controls, hover_window_ == bar_ ? hover_ : -1, hover_window_ == bar_ ? pressed_ : -1);
    });
}

void Session::PaintPanel() {
    RECT screen{};
    GetWindowRect(panel_, &screen);
    screen.right = screen.left + (panel_rect_.right - panel_rect_.left);
    screen.bottom = screen.top + (panel_rect_.bottom - panel_rect_.top);
    panel_canvas_.Paint(panel_, painter_, screen, scale_, dark_, [&] {
        const float s = scale_;
        const auto theme = painter_.Theme();
        const float w = static_cast<float>(screen.right - screen.left), h = static_cast<float>(screen.bottom - screen.top);
        painter_.Backdrop({0, 0, w, h}, acrylic_panel_);
        painter_.Text(L"实时拼接预览", {14 * s, 12 * s, w - 90 * s, 34 * s}, 13, theme.Ink(), true);
        const std::wstring count = Thousands(view_.segments) + L" 段";
        const float cw = painter_.Measure(count, 11.5f).x + 16 * s;
        const Box chip{w - 14 * s - cw, 13 * s, w - 14 * s, 33 * s};
        painter_.Fill(chip, 0x2a3b82f6, 10 * s);
        painter_.Text(count, chip, 11.5f, theme.Accent(), true, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Preview of the bottom of the stitched image.
        const Box box = PreviewBox();
        painter_.Fill(box, dark_ ? 0x66101826 : 0x80ffffff, 10 * s);
        if (view_.tail && view_.tail->Width() > 0) {
            ComPtr<ID2D1Bitmap> bitmap;
            const auto& tail = *view_.tail;
            const auto properties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
            if (SUCCEEDED(painter_.Target()->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(tail.Width()), static_cast<UINT32>(tail.Height())),
                    tail.pixels.data(), static_cast<UINT32>(tail.Width() * 4), properties, &bitmap))) {
                const float k = (box.right - box.left) / static_cast<float>(tail.Width());
                const float drawn = static_cast<float>(tail.Height()) * k;
                // Short results start at the top; long ones keep the newest rows visible.
                const float top = drawn <= box.bottom - box.top ? box.top : box.bottom - drawn;
                ComPtr<ID2D1RoundedRectangleGeometry> clip;
                painter_.Factory()->CreateRoundedRectangleGeometry(D2D1::RoundedRect(Rect(box), 10 * s, 10 * s), &clip);
                ComPtr<ID2D1Layer> layer;
                painter_.Target()->CreateLayer(&layer);
                painter_.Target()->PushLayer(D2D1::LayerParameters(Rect(box), clip.Get()), layer.Get());
                painter_.Target()->DrawBitmap(bitmap.Get(), D2D1::RectF(box.left, top, box.right, top + drawn), 1,
                    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                const auto thumb_y = [&](double composed_row) {
                    return top + static_cast<float>((composed_row * view_.thumb_scale - view_.tail_first) * k);
                };
                for (int seam : view_.seams) {
                    const float y = thumb_y(seam);
                    if (y < box.top || y > box.bottom) continue;
                    for (float x = box.left; x < box.right; x += 8 * s)
                        painter_.Line({x, y}, {std::min(box.right, x + 4 * s), y}, 0xa03b82f6, 1.2f * s);
                }
                // Current viewport inside the result.
                const float vy0 = std::max(box.top + s, thumb_y(std::max(0, view_.height - view_.frame_height)));
                const float vy1 = std::min(box.bottom - s, thumb_y(view_.height));
                if (vy1 > vy0) {
                    painter_.Fill({box.left + s, vy0, box.right - s, vy1}, 0x183b82f6, 4 * s);
                    painter_.Stroke({box.left + s, vy0, box.right - s, vy1}, theme.Accent(), 4 * s, 2 * s);
                }
                painter_.Target()->PopLayer();
            }
        } else {
            painter_.Text(L"正在截取第一屏…", box, 12, theme.Muted(), false, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        painter_.Stroke(box, theme.Border(), 10 * s, s);

        float y = box.bottom + 8 * s;
        // Notice card for anything that needs the user.
        if (view_.problem != Problem::None && view_.problem != Problem::Bottom) {
            const Box card{box.left, y, box.right, y + 110 * s};
            const bool error = view_.problem == Problem::Error || view_.problem == Problem::NoOverlap || view_.problem == Problem::LowMatch;
            const uint32_t tone = error ? 0xffef4444 : 0xff3b82f6;
            painter_.Fill(card, (tone & 0xffffff) | (dark_ ? 0x26000000 : 0x1a000000), 10 * s);
            painter_.Stroke(card, (tone & 0xffffff) | 0x66000000, 10 * s, s);
            painter_.Icon(error ? Glyph::Warning : Glyph::Info, {card.left + 8 * s, card.top + 8 * s, card.left + 32 * s, card.top + 32 * s}, tone);
            const wchar_t* heading = view_.problem == Problem::LowMatch ? L"重叠部分不太一致"
                : view_.problem == Problem::NoOverlap ? L"没有找到重叠部分"
                : view_.problem == Problem::NoScroll ? L"已切换到手动滚动"
                : view_.problem == Problem::Limit ? L"已达到高度上限" : L"出现问题";
            painter_.Text(heading, {card.left + 36 * s, card.top + 10 * s, card.right - 10 * s, card.top + 30 * s}, 12.5f, theme.Ink(), true);
            painter_.Text(view_.message, {card.left + 36 * s, card.top + 30 * s, card.right - 10 * s, card.top + 72 * s}, 11.5f,
                theme.Muted(), false, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, true);
            std::vector<ui::Control> controls;
            for (const auto& item : PanelItems()) {
                ui::Control c;
                c.id = item.id;
                c.kind = ui::Kind::Button;
                c.bounds = item.box;
                c.text = item.id == 20 ? L"仍然拼接" : item.id == 21 ? L"重试" : item.id == 5 ? L"撤回"
                    : item.id == 22 ? L"继续" : item.id == 7 ? L"完成" : L"放弃";
                c.primary = item.id == 20 || (item.id == 7 && view_.problem == Problem::NoOverlap);
                controls.push_back(std::move(c));
            }
            auto paint = painter_.Context(acrylic_panel_);
            ui::DrawControls(paint, theme, controls, hover_window_ == panel_ ? hover_ : -1, hover_window_ == panel_ ? pressed_ : -1);
            y = card.bottom + 8 * s;
        }
        // Detected structure chips.
        const auto chip_line = [&](const std::wstring& text, uint32_t tone) {
            const float tw = std::min(box.right - box.left, painter_.Measure(text, 11.5f).x + 26 * s);
            const Box c{box.left, y, box.left + tw, y + 22 * s};
            painter_.Fill(c, (tone & 0xffffff) | 0x22000000, 11 * s);
            painter_.Target()->FillEllipse(D2D1::Ellipse({c.left + 11 * s, (c.top + c.bottom) / 2}, 3 * s, 3 * s), painter_.Brush(tone));
            painter_.Text(text, {c.left + 19 * s, c.top, c.right - 6 * s, c.bottom}, 11.5f, theme.Ink(), false,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            y += 28 * s;
        };
        if (view_.header > 0) chip_line(L"已识别吸顶栏 " + std::to_wstring(view_.header) + L" px · 只保留一次", 0xff22c55e);
        if (view_.scrollbar > 0) chip_line(L"已识别滚动条 " + std::to_wstring(view_.scrollbar) + L" px · 预览时可去掉", 0xff22c55e);
        else if (view_.message.size() && (view_.problem == Problem::None || view_.problem == Problem::Bottom)) chip_line(view_.message, 0xff3b82f6);
        // Stats
        const float sy = h - 14 * s - 46 * s;
        const float col = (w - 28 * s) / 3;
        const std::wstring values[3]{Thousands(view_.height) + L" px", Thousands(view_.segments), EstimatePng(view_.width - view_.scrollbar, view_.height)};
        const wchar_t* labels[3]{L"高度", L"拼接段", L"预计 PNG"};
        painter_.Line({14 * s, sy - 6 * s}, {w - 14 * s, sy - 6 * s}, theme.Border(), s);
        for (int i = 0; i < 3; ++i) {
            const float x = 14 * s + col * static_cast<float>(i);
            painter_.Text(labels[i], {x, sy, x + col, sy + 18 * s}, 11, theme.Muted());
            painter_.Text(values[i], {x, sy + 17 * s, x + col, sy + 42 * s}, 15, theme.Ink(), true);
        }
        painter_.Text(view_.height >= kMaxHeight ? L"已到上限" : (L"上限 " + Thousands(kMaxHeight) + L" px"),
            {w - 14 * s - col, sy - 26 * s, w - 14 * s, sy - 8 * s}, 10.5f, theme.Muted(), false, DWRITE_TEXT_ALIGNMENT_TRAILING);
    });
}

void Session::Action(int id) {
    if (finished_) return;
    switch (id) {
    case 1: case 2:
        view_.mode = id == 1 ? Mode::Auto : Mode::Manual;
        view_.phase = Phase::Running;
        view_.problem = Problem::None;
        Send({Command::SetMode, id == 1 ? 0 : 1});
        break;
    case 3:
        if (view_.mode == Mode::Manual) return;
        view_.speed = static_cast<Speed>((static_cast<int>(view_.speed) + 1) % 3);
        Send({Command::SetSpeed, static_cast<int>(view_.speed)});
        break;
    case 4:
        if (view_.phase == Phase::Running) { view_.phase = Phase::Paused; Send({Command::Pause}); }
        else if (view_.phase == Phase::Paused) { view_.phase = Phase::Running; Send({Command::Resume}); }
        break;
    case 5: if (view_.can_undo) Send({Command::Undo}); break;
    case 6: Cancel(); return;
    case 7: if (view_.segments > 0) { view_.phase = Phase::Finishing; Send({Command::Finish}); } break;
    case 20: Send({Command::Force}); break;
    case 21: Send({Command::Skip}); break;
    case 22: view_.problem = Problem::None; view_.phase = Phase::Running; Send({Command::Resume}); break;
    default: return;
    }
    PaintAll();
}

void Session::Key(WPARAM key) {
    switch (key) {
    case VK_SPACE: Action(4); break;
    case 'A': Action(view_.mode == Mode::Auto ? 2 : 1); break;
    case 'S': Action(3); break;
    case VK_BACK: Action(5); break;
    case VK_RETURN: Action(7); break;
    case VK_ESCAPE: Cancel(); break;
    default: break;
    }
}

void Session::Cancel() {
    if (confirming_ || finished_) return;
    if (view_.segments >= 3) {
        confirming_ = true;
        const bool was_running = view_.phase == Phase::Running;
        if (was_running) Send({Command::Pause});
        const bool discard = ui::ShowThemedMessage(bar_, dark_, L"放弃长截图？",
            L"已经拼接了 " + Thousands(view_.segments) + L" 段（" + Thousands(view_.height) + L" px）。放弃后这些内容不会保存。",
            L"放弃", L"继续截图");
        confirming_ = false;
        if (!discard) {
            if (was_running) Send({Command::Resume});
            return;
        }
    }
    finished_ = true;
    for (HWND w : {frame_, bar_, panel_}) ShowWindow(w, SW_HIDE);
    if (closed_) closed_();
}

void Session::Pointer(HWND window, UINT message, POINT client) {
    const Point p{static_cast<float>(client.x), static_cast<float>(client.y)};
    int hit = -1;
    if (window == bar_) {
        for (const auto& item : BarItems()) if (item.id != 100 && Inside(item.box, p)) hit = item.id;
    } else if (window == panel_) {
        for (const auto& item : PanelItems()) if (Inside(item.box, p)) hit = item.id;
    }
    const auto repaint = [&] { if (window == bar_) PaintBar(); else PaintPanel(); };
    if (message == WM_MOUSEMOVE) {
        if (hit != hover_ || window != hover_window_) {
            const HWND previous = hover_window_;
            hover_ = hit;
            hover_window_ = window;
            if (previous && previous != window) { if (previous == bar_) PaintBar(); else if (previous == panel_) PaintPanel(); }
            repaint();
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
            TrackMouseEvent(&track);
        }
    } else if (message == WM_LBUTTONDOWN) {
        pressed_ = hit;
        hover_window_ = window;
        if (hit >= 0) SetCapture(window);
        repaint();
    } else if (message == WM_LBUTTONUP) {
        const int pressed = std::exchange(pressed_, -1);
        if (GetCapture() == window) ReleaseCapture();
        repaint();
        if (pressed >= 0 && pressed == hit) Action(hit);
    }
}

LRESULT CALLBACK Session::MouseHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && g_hooked && (wp == WM_MOUSEWHEEL || wp == WM_LBUTTONUP)) {
        const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
        if (!(info->flags & LLMHF_INJECTED) && Contains(g_hooked->region_, info->pt))
            PostMessageW(g_hooked->bar_, kUserInput, wp == WM_MOUSEWHEEL ? 1 : 0, 0);
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

LRESULT CALLBACK Session::Proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<Session*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<Session*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wp, lp);
    try {
        return self->Message(window, message, wp, lp);
    } catch (const std::exception&) {
        return DefWindowProcW(window, message, wp, lp);
    }
}

LRESULT Session::Message(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(window, &ps);
        EndPaint(window, &ps);
        return 0;
    }
    case WM_NCCALCSIZE: return 0;
    case WM_MOUSEACTIVATE: return window == bar_ ? MA_ACTIVATE : MA_NOACTIVATE;
    case WM_NCHITTEST: {
        if (window == frame_) return HTTRANSPARENT;
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(window, &p);
        const Point point{static_cast<float>(p.x), static_cast<float>(p.y)};
        if (window == bar_) {
            for (const auto& item : BarItems()) if (item.id != 100 && Inside(item.box, point)) return HTCLIENT;
            return HTCAPTION;
        }
        if (point.y < 40 * scale_) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP:
        Pointer(window, message, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        return 0;
    case WM_MOUSELEAVE:
        if (hover_window_ == window) {
            hover_ = -1;
            if (window == bar_) PaintBar(); else PaintPanel();
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (pressed_ >= 0) { pressed_ = -1; if (window == bar_) PaintBar(); else if (window == panel_) PaintPanel(); }
        return 0;
    case WM_KEYDOWN: Key(wp); return 0;
    case WM_EXITSIZEMOVE:
        if (window == bar_) GetWindowRect(bar_, &bar_rect_);
        if (window == panel_) GetWindowRect(panel_, &panel_rect_);
        return 0;
    case WM_DPICHANGED: return 0;
    case kSnapshot: {
        if (finished_) return 0;
        {
            std::lock_guard lock(mutex_);
            view_ = published_;
        }
        if (hover_window_ == panel_) hover_ = -1;
        PaintAll();
        return 0;
    }
    case kDone: {
        std::unique_ptr<CaptureResult> result;
        {
            std::lock_guard lock(mutex_);
            result = std::move(result_);
        }
        if (!result || finished_) return 0;
        finished_ = true;
        for (HWND w : {frame_, bar_, panel_}) ShowWindow(w, SW_HIDE);
        if (done_) done_(std::move(result));
        return 0;
    }
    case kUserInput:
        if (finished_ || confirming_) return 0;
        if (wp && view_.mode == Mode::Auto && view_.phase != Phase::Finishing && view_.phase != Phase::Starting) {
            // Wheel over the region: the user takes over.
            view_.mode = Mode::Manual;
            view_.phase = Phase::Running;
            view_.problem = Problem::None;
            Send({Command::SetMode, 1});
            PaintAll();
        }
        input_tick_ = GetTickCount64();
        return 0;
    case WM_CLOSE: Cancel(); return 0;
    default: break;
    }
    return DefWindowProcW(window, message, wp, lp);
}
}
