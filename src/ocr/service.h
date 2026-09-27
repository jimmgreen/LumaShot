#pragma once
#include "ocr/protocol.h"
#include "capture/frame.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <memory>
namespace lumashot::ocr {
struct Completion {uint64_t id{},version{};Text text;std::wstring error;};
class Service {
public:
    Service(HWND notify,UINT message);
    ~Service();
    void Submit(uint64_t id,uint64_t version,std::shared_ptr<const Frame> image);
    void Cancel(uint64_t id);
    std::vector<Completion> Take();
private:
    struct Job {uint64_t id{},version{};std::shared_ptr<const Frame> image;std::shared_ptr<std::atomic_bool> canceled;};
    void Run();void StartChild();void StopChild();
    void Read(void* buffer,size_t size,const Job& job);
    HWND notify_;UINT message_;
    std::mutex mutex_;std::condition_variable cv_;std::deque<Job> jobs_;
    std::vector<Completion> completed_;std::optional<Job> active_;
    std::atomic_bool stopping_{};
    Handle process_,input_,output_,job_object_;
    std::thread thread_;
};
}
