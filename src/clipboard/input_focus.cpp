#include "clipboard/input_focus.h"
#include <ole2.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <atomic>
#include <cwchar>
#include <thread>

namespace lumashot::clipboard {
using Microsoft::WRL::ComPtr;
InputFocusSnapshot InputFocusSnapshot::Capture(HWND foreground) {
    InputFocusSnapshot result;
    result.foreground = foreground;
    const DWORD thread = foreground ? GetWindowThreadProcessId(foreground, &result.process) : 0;
    if (!thread || !GetGUIThreadInfo(thread, &result.gui)) result.gui = {sizeof(GUITHREADINFO)};
    if (result.gui.hwndFocus != foreground && !IsChild(foreground, result.gui.hwndFocus)) result.gui.hwndFocus = nullptr;
    const auto focused = result.gui.hwndFocus;
    result.caret_visible = focused && result.gui.hwndCaret &&
        (result.gui.hwndCaret == focused || IsChild(focused, result.gui.hwndCaret)) &&
        (result.gui.flags & GUI_CARETBLINKING) && result.gui.rcCaret.bottom > result.gui.rcCaret.top;
    return result;
}
bool InputFocusSnapshot::Current(HWND candidate) const {
    if (!foreground || candidate != foreground || !IsWindow(foreground)) return false;
    DWORD current_process{};
    const DWORD thread = GetWindowThreadProcessId(foreground, &current_process);
    GUITHREADINFO info{sizeof(info)};
    return thread && current_process == process && GetGUIThreadInfo(thread, &info) &&
        info.hwndFocus == gui.hwndFocus;
}
std::optional<bool> NativeEditableInput(const InputFocusSnapshot& snapshot) {
    const HWND focus = snapshot.gui.hwndFocus;
    if (!IsWindow(focus) || !IsWindowEnabled(focus) || !IsWindowVisible(focus)) return false;
    wchar_t name[128]{};
    if (!GetClassNameW(focus, name, 128)) return false;
    if (_wcsicmp(name, L"Edit") == 0 || _wcsnicmp(name, L"RichEdit", 8) == 0)
        return (GetWindowLongPtrW(focus, GWL_STYLE) & ES_READONLY) == 0;
    if (_wcsicmp(name, L"ComboBox") == 0)
        return (GetWindowLongPtrW(focus, GWL_STYLE) & 3) != CBS_DROPDOWNLIST;
    if (_wcsicmp(name, L"Scintilla") == 0) {
        DWORD_PTR read_only{};
        // SCI_GETREADONLY reads only editability, never the document text.
        if (SendMessageTimeoutW(focus, 2140, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 50, &read_only))
            return read_only == 0;
        return false;
    }
    for (const auto* control : {L"Button", L"Static", L"SysListView32", L"SysTreeView32",
                               L"SysTabControl32", L"ToolbarWindow32", L"ScrollBar"})
        if (_wcsicmp(name, control) == 0) return false;
    return std::nullopt;
}
namespace {
std::optional<bool> CachedBool(IUIAutomationElement* element, PROPERTYID property) {
    VARIANT value{};
    std::optional<bool> result;
    if (SUCCEEDED(element->GetCachedPropertyValueEx(property, TRUE, &value)) && value.vt == VT_BOOL)
        result = value.boolVal != VARIANT_FALSE;
    VariantClear(&value);
    return result;
}
bool AutomationEditableInput(const InputFocusSnapshot& focus) {
    if (!focus.Current(GetForegroundWindow())) return false;
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return focus.caret_visible;
    struct ComCleanup { ~ComCleanup() { CoUninitialize(); } } cleanup;
    ComPtr<IUIAutomation2> automation;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&automation)))) return focus.caret_visible;
    if (FAILED(automation->put_ConnectionTimeout(80)) || FAILED(automation->put_TransactionTimeout(80)))
        return focus.caret_visible;
    ComPtr<IUIAutomationCacheRequest> cache;
    if (FAILED(automation->CreateCacheRequest(&cache))) return focus.caret_visible;
    cache->put_TreeScope(TreeScope_Element);
    for (const PROPERTYID property : {UIA_ProcessIdPropertyId, UIA_HasKeyboardFocusPropertyId,
            UIA_IsEnabledPropertyId, UIA_ControlTypePropertyId, UIA_ValueIsReadOnlyPropertyId,
            UIA_IsTextEditPatternAvailablePropertyId})
        if (FAILED(cache->AddProperty(property))) return focus.caret_visible;
    ComPtr<IUIAutomationElement> element;
    if (FAILED(automation->GetFocusedElementBuildCache(cache.Get(), &element)) || !element)
        return focus.caret_visible;
    int process{};
    CONTROLTYPEID type{};
    DWORD focused_process{};
    GetWindowThreadProcessId(focus.gui.hwndFocus, &focused_process);
    if (FAILED(element->get_CachedProcessId(&process)) ||
        (static_cast<DWORD>(process) != focus.process && static_cast<DWORD>(process) != focused_process) ||
        FAILED(element->get_CachedControlType(&type))) return false;
    AccessibleInputState state;
    state.enabled = CachedBool(element.Get(), UIA_IsEnabledPropertyId).value_or(false);
    state.focused = CachedBool(element.Get(), UIA_HasKeyboardFocusPropertyId).value_or(false);
    state.edit = type == UIA_EditControlTypeId;
    state.document = type == UIA_DocumentControlTypeId;
    state.text_edit = CachedBool(element.Get(), UIA_IsTextEditPatternAvailablePropertyId).value_or(false);
    state.read_only = CachedBool(element.Get(), UIA_ValueIsReadOnlyPropertyId);
    if (type == UIA_ComboBoxControlTypeId) state.edit = state.read_only == false;
    if ((state.edit || state.document) && !state.read_only.has_value()) {
        ComPtr<IUIAutomationTextPattern> text;
        ComPtr<IUIAutomationTextRange> range;
        if (SUCCEEDED(element->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&text))) && text &&
            SUCCEEDED(text->get_DocumentRange(&range)) && range) {
            VARIANT value{};
            if (SUCCEEDED(range->GetAttributeValue(UIA_IsReadOnlyAttributeId, &value)) && value.vt == VT_BOOL)
                state.read_only = value.boolVal != VARIANT_FALSE;
            VariantClear(&value);
        }
    }
    return focus.Current(GetForegroundWindow()) && EditableAccessibleInput(state);
}
}
struct InputFocusProbe::Job {
    std::atomic<bool> done{};
    bool editable{};
};
bool InputFocusProbe::Begin(const InputFocusSnapshot& focus) {
    if (job_ && !job_->done.load(std::memory_order_acquire)) return false;
    auto job = std::make_shared<Job>();
    try {
        std::thread([job, focus] {
            try { job->editable = AutomationEditableInput(focus); }
            catch (...) { job->editable = false; }
            job->done.store(true, std::memory_order_release);
        }).detach();
    } catch (...) { return false; }
    job_ = std::move(job);
    return true;
}
std::optional<bool> InputFocusProbe::Result() const {
    if (!job_ || !job_->done.load(std::memory_order_acquire)) return std::nullopt;
    return job_->editable;
}
}
