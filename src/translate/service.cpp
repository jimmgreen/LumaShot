#include "translate/service.h"
#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace lumashot::translate {
struct Service::Impl {
    struct Task {
        std::jthread thread;
        std::optional<Outcome> outcome;
        std::shared_ptr<std::atomic<bool>> done = std::make_shared<std::atomic<bool>>(false);
    };
    HWND owner{};
    UINT message{};
    mutable std::mutex lock;
    std::map<uint64_t, Task> tasks;
    std::vector<std::pair<std::jthread, std::shared_ptr<std::atomic<bool>>>> retired;
    uint64_t next{1};

    void Prune() {
        std::erase_if(retired, [](auto& item) {
            if (!item.second->load()) return false;
            if (item.first.joinable()) item.first.join();
            return true;
        });
    }
};

Service::Service(HWND owner, UINT message) : impl_(std::make_unique<Impl>()) {
    impl_->owner = owner;
    impl_->message = message;
}

Service::~Service() {
    std::vector<std::jthread> threads;
    {
        std::lock_guard guard(impl_->lock);
        for (auto& [id, task] : impl_->tasks) { task.thread.request_stop(); threads.push_back(std::move(task.thread)); }
        for (auto& item : impl_->retired) { item.first.request_stop(); threads.push_back(std::move(item.first)); }
        impl_->tasks.clear();
        impl_->retired.clear();
        impl_->owner = nullptr;
    }
    for (auto& thread : threads) if (thread.joinable()) thread.join();
}

uint64_t Service::Submit(Credentials credentials, Job job, http::Limits limits) {
    std::lock_guard guard(impl_->lock);
    impl_->Prune();
    const uint64_t id = impl_->next++;
    auto& task = impl_->tasks[id];
    auto done = task.done;
    Impl* impl = impl_.get();
    task.thread = std::jthread([impl, id, done, credentials = std::move(credentials), job = std::move(job), limits](std::stop_token stop) mutable {
        auto outcome = Run(credentials, job, stop, limits);
        credentials.key.assign(credentials.key.size(), '\0');
        HWND owner{};
        UINT message{};
        {
            std::lock_guard inner(impl->lock);
            if (const auto it = impl->tasks.find(id); it != impl->tasks.end() && !stop.stop_requested()) {
                it->second.outcome = std::move(outcome);
                owner = impl->owner;
                message = impl->message;
            }
        }
        done->store(true);
        if (owner) PostMessageW(owner, message, static_cast<WPARAM>(id), 0);
    });
    return id;
}

void Service::Cancel(uint64_t id) {
    std::lock_guard guard(impl_->lock);
    const auto it = impl_->tasks.find(id);
    if (it == impl_->tasks.end()) return;
    it->second.thread.request_stop();
    impl_->retired.emplace_back(std::move(it->second.thread), it->second.done);
    impl_->tasks.erase(it);
    impl_->Prune();
}

std::optional<Outcome> Service::Take(uint64_t id) {
    std::jthread finished;
    std::optional<Outcome> outcome;
    {
        std::lock_guard guard(impl_->lock);
        const auto it = impl_->tasks.find(id);
        if (it == impl_->tasks.end() || !it->second.outcome) return std::nullopt;
        outcome = std::move(it->second.outcome);
        finished = std::move(it->second.thread);
        impl_->tasks.erase(it);
        impl_->Prune();
    }
    if (finished.joinable()) finished.join();
    return outcome;
}

size_t Service::Pending() const {
    std::lock_guard guard(impl_->lock);
    return impl_->tasks.size();
}
}
