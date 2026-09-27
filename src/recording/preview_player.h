#pragma once
#include <mfplay.h>
#include <wrl/client.h>
#include <atomic>
#include <memory>
#include <mutex>

namespace lumashot::recording {
// MFPlay invokes callbacks on its own threads. Never access Ui or transfer raw
// owning pointers through window messages; a detached mailbox outlives callbacks.
struct PreviewPlayerMailbox {
    std::mutex mutex;
    HWND window{};
    UINT message{};
    WPARAM generation{};
    Microsoft::WRL::ComPtr<IMFPMediaItem> item;
    HRESULT error{S_OK};
    bool ready{},position_set{},ended{},started{},paused{};
};
class PreviewPlayerCallback final : public IMFPMediaPlayerCallback {
    std::atomic<ULONG> references_{1};
    std::shared_ptr<PreviewPlayerMailbox> mailbox_;
public:
    explicit PreviewPlayerCallback(std::shared_ptr<PreviewPlayerMailbox> mailbox):mailbox_(std::move(mailbox)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** value) override {
        if(!value)return E_POINTER;
        *value=nullptr;
        if(iid!=__uuidof(IUnknown)&&iid!=__uuidof(IMFPMediaPlayerCallback))return E_NOINTERFACE;
        *value=static_cast<IMFPMediaPlayerCallback*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++references_;}
    ULONG STDMETHODCALLTYPE Release() override {const ULONG count=--references_;if(!count)delete this;return count;}
    void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* event) override {
        if(!event)return;
        if(SUCCEEDED(event->hrEvent)&&event->eEventType!=MFP_EVENT_TYPE_MEDIAITEM_CREATED&&event->eEventType!=MFP_EVENT_TYPE_MEDIAITEM_SET&&event->eEventType!=MFP_EVENT_TYPE_POSITION_SET&&event->eEventType!=MFP_EVENT_TYPE_PLAYBACK_ENDED&&event->eEventType!=MFP_EVENT_TYPE_PLAY&&event->eEventType!=MFP_EVENT_TYPE_PAUSE)return;
        std::lock_guard lock(mailbox_->mutex);
        if(!mailbox_->window)return;
        if(FAILED(event->hrEvent))mailbox_->error=event->hrEvent;
        else if(event->eEventType==MFP_EVENT_TYPE_MEDIAITEM_CREATED)
            mailbox_->item=MFP_GET_MEDIAITEM_CREATED_EVENT(event)->pMediaItem;
        else if(event->eEventType==MFP_EVENT_TYPE_MEDIAITEM_SET)mailbox_->ready=true;
        else if(event->eEventType==MFP_EVENT_TYPE_POSITION_SET)mailbox_->position_set=true;
        else if(event->eEventType==MFP_EVENT_TYPE_PLAY)mailbox_->started=true;
        else if(event->eEventType==MFP_EVENT_TYPE_PAUSE)mailbox_->paused=true;
        else mailbox_->ended=true;
        PostMessageW(mailbox_->window,mailbox_->message,mailbox_->generation,0);
    }
};
}
