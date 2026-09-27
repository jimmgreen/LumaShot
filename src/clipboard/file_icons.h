#pragma once
#include "capture/frame.h"
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <string>
namespace lumashot::clipboard {
constexpr UINT FileIconReady=WM_APP+92;
std::wstring FirstFilePath(const std::wstring& paths);
int FileIconResolution(int pixels);
// Shell/file-system calls belong exclusively to the worker thread.
std::shared_ptr<const Frame> LoadFileIcon(const std::wstring& path,int pixels);
class FileIconCache {
public:
    static constexpr size_t MaxEntries=16;
    FileIconCache()=default;
    ~FileIconCache();
    std::shared_ptr<const Frame> Get(const std::wstring& path,int pixels,HWND notify);
    void Clear();
    bool WaitIdle(unsigned milliseconds);
    size_t Size();
private:
    using Key=std::pair<std::wstring,int>;
    struct Entry {std::shared_ptr<const Frame> image;bool ready{};uint64_t use{};};
    struct Job {Key key;uint64_t generation{};HWND notify{};};
    void Work();
    std::mutex mutex_;std::condition_variable cv_;std::map<Key,Entry> cache_;std::deque<Job> jobs_;
    uint64_t generation_{},clock_{};bool stop_{},busy_{};std::thread worker_;
};
}
