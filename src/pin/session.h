#pragma once
#include "model/document.h"
#include "model/pin_style.h"
#include "pin/selection_tools.h"
#include <filesystem>
#include <memory>
#include <map>
namespace lumashot {
struct PinSessionRecord {
    uint64_t id{},revision{1},storage_nonce{};Point origin{};float zoom{1};PinStyle style{DefaultPinStyle};bool locked{},recognize{};
    std::shared_ptr<const Frame> image,ocr_image,base;
    std::vector<Mark> annotations;std::vector<SelectionMark> decorations;
};
class PinSessionStore {
public:
    explicit PinSessionStore(std::filesystem::path directory);
    static std::filesystem::path DefaultDirectory();
    std::vector<PinSessionRecord> Load(unsigned& skipped);
    void Save(const std::vector<PinSessionRecord>& records);
private:
    std::filesystem::path directory_;uint64_t nonce_{};std::map<uint64_t,uint64_t> written_;std::vector<PinSessionRecord> unresolved_;
};
}
