#pragma once
#include "clipboard/history.h"
#include <optional>
#include <wincodec.h>
#include <wrl/client.h>
namespace lumashot::clipboard {
// A null result is either unsupported/private content or an empty clipboard.
std::optional<Entry> Read(HWND owner,bool& busy);
bool Write(HWND owner,const Entry& entry,bool plain_text=false);
Microsoft::WRL::ComPtr<IWICBitmapSource> Thumbnail(const Entry& entry,UINT edge=600,uint64_t max_pixels=100000000);
}
