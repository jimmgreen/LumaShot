#pragma once
#include "translate/engine.h"
#include <windows.h>
#include <cstdint>
#include <memory>
#include <optional>

// Runs translation jobs off the UI thread. Each job owns a short-lived
// std::jthread (nothing runs while idle). Completion posts `message` with
// WPARAM = job id to the owner; the owner then calls Take(id).
namespace lumashot::translate {
class Service {
public:
    Service(HWND owner, UINT message);
    ~Service();
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
    uint64_t Submit(Credentials credentials, Job job, http::Limits limits = {});
    void Cancel(uint64_t id);
    std::optional<Outcome> Take(uint64_t id);
    size_t Pending() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
