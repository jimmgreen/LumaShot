#pragma once
#include "ocr/service.h"
#include "export/clipboard.h"
#include "ui/render.h"
#include "model/pin_style.h"
#include "pin/session.h"
#include "app/deferred_writer.h"
#include <future>
#include <atomic>
#include <functional>
#include <map>
namespace lumashot {
constexpr UINT kOcrReady=WM_APP+20,kPinSaveReady=WM_APP+21,kPinSessionError=WM_APP+89;
class PinManager {
public:
    explicit PinManager(HWND host);
    ~PinManager();
    void Create(Frame image,Frame ocr_image,POINT position,std::optional<Frame> base=std::nullopt,Document annotations={},bool recognize=true,const PinSessionRecord* restored=nullptr);
    bool EnableSession(const std::filesystem::path& directory);
    bool FlushSession();
    void PreserveSession();
    void Ready();void Saved();
    void RefreshAppearance();
    void CompleteAnnotation(uint64_t id,std::optional<Frame> image,Document annotations={},std::optional<Frame> base=std::nullopt,bool recognize=true);
    std::function<bool(uint64_t,std::shared_ptr<const Frame>,Document,RECT)> annotate;
    std::function<bool()> dark_theme;
    std::function<int()> clipboard_format;
    std::function<PinStyle()> sticker_style;
private:
    friend struct PinTest;
    friend struct PinSessionLifecycleTest;
    std::vector<PinSessionRecord> SessionSnapshot()const;
    void SessionChanged();
    std::unique_ptr<PinSessionStore> session_store_;
    std::unique_ptr<DeferredWriter<std::vector<PinSessionRecord>>> session_writer_;
    bool restoring_session_{},preserving_session_{},preserve_ok_{true};
    std::atomic_bool session_warning_{};
    struct Pin;
    static LRESULT CALLBACK Proc(HWND,UINT,WPARAM,LPARAM);
    static LRESULT CALLBACK ToolsProc(HWND,UINT,WPARAM,LPARAM);
    void UpdateTools(Pin&);
    void ToolCommand(Pin&,int);
    void Annotate(Pin&);
    void ToggleLock(Pin&);
    void ApplyPaper(Pin&,Point);
    void Paint(Pin& pin);
    void Zoom(Pin&,int delta,POINT anchor);
    void Draw(Pin& pin,ID2D1RenderTarget* target);
    void Recognize(Pin& pin);
    void Menu(Pin& pin,POINT point);
    void Save(Pin& pin);
    void Copy(Pin& pin);
    struct SaveResult {uint64_t id{};std::wstring error;std::unique_ptr<ClipboardImage> clipboard_image;};
    HWND host_;ocr::Service service_;uint64_t next_id_{1};
    std::map<uint64_t,std::unique_ptr<Pin>> pins_;
    std::vector<std::future<SaveResult>> saves_;
    bool processing_saves_{};
};
}



