#pragma once
#include "clipboard/preview_data.h"
#include <list>

namespace lumashot::clipboard {
// UI-owned, bounded decoded pixels only. Never retains original clipboard formats.
class PreviewCache {
public:
    static constexpr size_t Limit = 8 * 1024 * 1024;
    explicit PreviewCache(size_t limit = Limit) : limit_(limit) {}
    std::shared_ptr<const Frame> Find(uint64_t id) {
        for (auto it = items_.begin(); it != items_.end(); ++it) if (it->first == id) {
            auto image = it->second;
            items_.splice(items_.begin(), items_, it);
            return image;
        }
        return {};
    }
    void Put(uint64_t id, std::shared_ptr<const Frame> image) {
        if (!image || image->pixels.size() * 4 > limit_) return;
        for (auto it = items_.begin(); it != items_.end(); ++it) if (it->first == id) {
            bytes_ -= it->second->pixels.size() * 4;items_.erase(it);break;
        }
        const auto bytes = image->pixels.size() * 4;
        while (bytes_ + bytes > limit_) {bytes_ -= items_.back().second->pixels.size() * 4;items_.pop_back();}
        items_.emplace_front(id, std::move(image));bytes_ += bytes;
    }
    void Clear() {items_.clear();bytes_ = 0;}
    size_t Bytes() const {return bytes_;}
private:
    std::list<std::pair<uint64_t, std::shared_ptr<const Frame>>> items_;
    size_t bytes_{}, limit_;
};
}
