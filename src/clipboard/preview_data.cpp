#include "clipboard/preview_data.h"
#include "clipboard/native.h"

namespace lumashot::clipboard {
PreviewData PreparePreview(const Entry& entry) {
    PreviewData data;
    if(entry.kind==Kind::Files){
        size_t start=0;while(start<entry.text.size()&&data.file_count<256){
            const auto end=entry.text.find(L'\n',start);const auto path=entry.text.substr(start,end==std::wstring::npos?end:end-start);
            const auto slash=path.find_last_of(L"\\/");auto name=path.substr(slash==std::wstring::npos?0:slash+1,260);
            if(!name.empty()&&name.back()>=0xd800&&name.back()<=0xdbff)name.pop_back();
            if(data.file_count)data.text+=L"\n";data.text+=std::to_wstring(++data.file_count)+L". "+name;
            if(end==std::wstring::npos)break;start=end+1;
        }
        return data;
    }
    if (entry.kind != Kind::Image) {
        data.text = entry.text.substr(0, PreviewData::MaxTextChars);
        if (data.text.size() < entry.text.size()) {
            data.truncated=true;
            if (!data.text.empty() && data.text.back() >= 0xd800 && data.text.back() <= 0xdbff)
                data.text.pop_back();
            data.text += L"…";
        }
        return data;
    }
    auto source = Thumbnail(entry, PreviewData::MaxEdge, PreviewData::MaxSourcePixels);
    UINT width{}, height{};
    if (source && SUCCEEDED(source->GetSize(&width, &height)) && width && height &&
        width <= PreviewData::MaxEdge && height <= PreviewData::MaxEdge) {
        auto frame = MakeFrame({0, 0, static_cast<LONG>(width), static_cast<LONG>(height)}, 0);
        if (SUCCEEDED(source->CopyPixels(nullptr, width * 4,
            static_cast<UINT>(frame.pixels.size() * 4), reinterpret_cast<BYTE*>(frame.pixels.data())))) {
            data.image = std::make_shared<Frame>(std::move(frame));
            return data;
        }
    }
    data.error = L"图片无法预览：数据损坏、格式不支持或超过 32M 像素限制。可尝试复制原内容。";
    return data;
}
}
