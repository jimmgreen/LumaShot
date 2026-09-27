#pragma once
#include "app/preferences.h"
#include "app/capture_ipc.h"
#include "app/hotkey_policy.h"
#include "clipboard/panel.h"
#include "app/deferred_writer.h"
#include "recording/process.h"
#include "export/clipboard.h"
#include "capture/cursor.h"
#include "capture/elements.h"
#include "ui/render.h"
#include "ui/magnifier.h"
#include "pin/pin.h"
#include "model/selection.h"
#include "ui/text_editor.h"
#include <mutex>
#include <thread>
#include <span>

namespace lumashot {
class Application {
public:
    int Run(bool capture_now, bool demo = false, bool diagnostic_session = false);
    ~Application();
    int RunIpcDemo(){ipc_demo_=true;return Run(false,true,true);}
    int RunForClient(const std::filesystem::path& output, bool demo=false);
private:
    friend struct ColdStartMemoryTest;
    friend struct HotkeyPolicyTest;
    friend struct ReselectTest;
    friend struct RecordingCoexistTest;
    friend struct RecordingEntryTest;
    friend struct SelectedPropertiesTest;
    friend struct TextEditTest;
    friend struct CaptureLatencyTest;
    friend struct MenuFocusTest;
    friend struct SettingsCoexistTest;
    friend struct CapturePinTest;
    friend struct ToolbarMotionTest;
    struct View { Application* app{}; RECT bounds{}; HWND window{}; std::unique_ptr<Renderer> renderer; bool needs_paint{}; };
    struct Result { std::unique_ptr<ClipboardImage> clipboard_image; Frame frame, acrylic, cursor_patch, ocr_frame;std::optional<Frame> annotation_base;Document annotations; std::wstring error; bool capture{}, saved{}, pinned{}, recognize{}; uint64_t generation{},editing{};bool edit_copy{}; };
    static LRESULT CALLBACK MainProc(HWND,UINT,WPARAM,LPARAM);
    static LRESULT CALLBACK OverlayProc(HWND,UINT,WPARAM,LPARAM);
    static LRESULT CALLBACK EditProc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
    static LRESULT CALLBACK TextHostProc(HWND,UINT,WPARAM,LPARAM);
    void Start();
    DWORD WaitForWork(std::span<const HANDLE> extra = {});
    bool AnnotatePin(uint64_t,std::shared_ptr<const Frame>,Document,RECT);
    void UpdatePinEditRegion();
    void Finish(bool save,bool pin=false,bool recognize=false);
    void ResultReady();
    void ElementsReady();
    void Cancel(bool close_demo=true);
    void ShowViews();
    void CreateViews();
    void UpdateMagnifier();
    void Invalidate();
    void PointerDown(View&,Point);
    Point PenSnapPoint(Point,std::optional<Point> anchor=std::nullopt);
    void PenHint();
    void PolylineClick(Point);
    void PolylinePreview(Point);
    void EndPolyline(bool cancel);
    void UndoPolylinePoint();
    std::vector<Point> line_vertices_;
    void PointerDoubleClick(View&,Point);
    void PointerMove(View&,Point,WPARAM);
    void PointerUp(View&,Point);
    void Key(View&,WPARAM);
    void Command(int id);
    void ResetSessionTool();
    void ApplyToolbarDropdown(int index);
    bool HasPropertyTarget()const;
    void SyncSelectedProperties();
    void ApplySelectedProperties(bool preview=false);
    std::optional<Mark> property_original_;
    float property_scale_{1};
    void ClosePicker(bool cancel=false);
    void RememberProperties();
    void FlushProperties();
    void DeferProperties();
    bool properties_dirty_{};
    bool ocr_available_{true};
    std::vector<uint32_t> recent_colors_;
    void TrayMenu();
    void ConfigureHotkeyPolicy();
    void RefreshHotkeys();
    void ToggleHotkeyPolicy(UINT choice);
    bool hotkeys_suspended_{},hotkeys_initialized_{};
    std::function<bool()> game_active_{FullScreenGameActive};
    void Settings();
    void UpdateToolbar(POINT point);
    void AnimateToolbar();
    void SyncToolbarMotion(uint64_t now=GetTickCount64());
    void TickToolbarMotion(uint64_t now=GetTickCount64());
    void StopToolbarMotion();
    Box WindowAt(POINT point) const;
    void BeginText(View&,Point,int existing=-1,bool badge_label=false);
    void CommitText(bool cancel=false);
    void ResizeTextEditor();
    void PaintTextEditor(HDC);
    void Notice(const std::wstring& text);
    HWND Owner() const;
    HWND main_{},edit_{},edit_host_{},edit_owner_{};
    HICON tray_icon_{};
    HFONT edit_font_{};
    HBRUSH edit_backdrop_{};
    HBITMAP edit_caret_{};
    TextRenderer edit_text_renderer_;
    Microsoft::WRL::ComPtr<ID2D1Factory> edit_text_factory_;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> edit_text_target_;
    Microsoft::WRL::ComPtr<IDWriteFactory> edit_text_writer_;
    std::wstring edit_ime_text_,edit_ime_original_;
    DWORD edit_ime_first_{},edit_ime_last_{};
    uint64_t edit_text_glyphs_{};
    std::unique_ptr<DibSurface> edit_native_surface_;
    SIZE edit_native_size_{};
    std::unique_ptr<TextEditorFrame> edit_frame_;
    RECT edit_work_{};
    uint32_t edit_color_{};
    uint32_t edit_display_color_{};
    uint32_t edit_surface_color_{};
    int edit_min_height_{},edit_max_height_{};
    bool edit_composing_{},edit_initializing_{},edit_badge_label_{};
    std::wstring edit_previous_hint_;
    std::wstring edit_family_;
    float edit_size_{};
    bool edit_bold_{};
    TextAlign edit_align_{TextAlign::Left};
    TextBackground edit_background_{TextBackground::None};
    int edit_index_{-1};
    Point edit_point_{};
    Preferences preferences_;
    std::unique_ptr<ClipboardPanel> clipboard_panel_;
    // All settings writes funnel through one worker so debounced saves never
    // block the UI while the dialog still receives a synchronous outcome.
    DeferredWriter<Preferences> settings_writer_{[](const Preferences& value){value.Save();}};
    RecordingProcess recording_process_;
    bool settings_open_{};int recording_finish_wait_{};
    std::shared_ptr<Frame> frame_,acrylic_,cursor_patch_;
    std::unique_ptr<PinManager> pins_;
    std::vector<std::unique_ptr<View>> views_;
    std::vector<RECT> monitors_;
    std::vector<RECT> windows_;
    std::vector<ElementWindow> element_windows_;
    std::vector<ElementRegion> element_regions_;
    ElementScanner element_scanner_;
    double capture_started_{};
    Document document_;
    std::optional<Mark> draft_;
    ViewState state_;
    ui::LinearTransition toolbar_transition_;
    ui::ToolbarMotion toolbar_motion_;
    bool toolbar_motion_timer_{},toolbar_effects_{true};int toolbar_pressed_{-1};
    RECT toolbar_monitor_{};
    Magnifier magnifier_;
    std::jthread worker_;
    std::mutex mutex_;
    std::unique_ptr<Result> result_;
    uint64_t generation_{},pin_edit_id_{};
    std::shared_ptr<const Frame> pin_edit_source_;RECT pin_edit_bounds_{};bool pin_edit_copy_{};
    bool pending_{},active_{},moving_{},resizing_{},mark_moving_{},demo_{};
    bool diagnostic_session_{};
    bool ipc_demo_{};
    std::unique_ptr<CaptureIpcSession> ipc_client_;
    std::filesystem::path client_output_;
    int client_exit_{2};
    Point start_{},previous_{};
    Box original_selection_{};
    int resize_handle_{-1};
    int mark_handle_{-1};std::optional<Mark> edit_original_;bool mark_checkpointed_{};
    UINT taskbar_created_{};
};
}






