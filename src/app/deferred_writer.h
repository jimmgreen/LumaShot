#pragma once
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <system_error>
#include <thread>

namespace lumashot {
// Single background owner for last-write-wins persistence (settings.ini).
// All calls must come from the owning thread; only the worker touches saved
// values concurrently. Request coalesces to the latest value and never blocks
// on disk I/O. Flush drains the queue on the worker and reports the last
// outcome. A failed save keeps its value queued and is retried by the next
// Request or Flush; the destructor performs one final flush before joining.
template<class T>
class DeferredWriter {
public:
    using Saver=std::function<void(const T&)>;
    DeferredWriter()=default;
    DeferredWriter(Saver saver,std::function<void()> thread_begin={},std::function<void()> thread_end={})
        :saver_(std::move(saver)),thread_begin_(std::move(thread_begin)),thread_end_(std::move(thread_end)){}
    ~DeferredWriter(){
        Flush();
        {std::lock_guard lock(mutex_);exit_=true;}
        cv_.notify_all();
        if(worker_.joinable())worker_.join();
    }
    DeferredWriter(const DeferredWriter&)=delete;
    DeferredWriter& operator=(const DeferredWriter&)=delete;
    void Request(const T& value){
        {std::lock_guard lock(mutex_);pending_=std::make_shared<const T>(value);started_=true;++requested_;}
        if(!worker_.joinable()){
            try{worker_=std::thread([this]{
                if(thread_begin_)thread_begin_();
                Work();
                if(thread_end_)thread_end_();
            });}catch(const std::system_error&){/* Flush falls back to inline saving. */}
        }
        cv_.notify_all();
    }
    void DropPending(){std::lock_guard lock(mutex_);pending_.reset();}
    bool Flush(){
        std::unique_lock lock(mutex_);
        if(!started_)return true;
        if(!worker_.joinable())return DrainInline(lock);
        if(!busy_&&!pending_)return true;
        // completed_ changes on every finished attempt, so a failure recorded
        // just before this snapshot still wakes the wait through the retry.
        if(pending_)++requested_; // wake the idle worker for the queued value
        const unsigned seen=completed_;
        cv_.notify_all();
        cv_.wait(lock,[&]{return completed_!=seen&&!busy_;});
        return !pending_;
    }
private:
    bool DrainInline(std::unique_lock<std::mutex>& lock){
        if(!pending_)return true;
        auto value=std::move(pending_);pending_.reset();
        lock.unlock();
        bool ok=true;
        try{saver_(*value);}catch(...){ok=false;}
        lock.lock();
        if(!ok)pending_=std::move(value);
        return !pending_;
    }
    void Work(){
        std::unique_lock lock(mutex_);
        unsigned handled{};
        for(;;){
            // A failed save stays queued but is only retried when the owner
            // asks again (Request/Flush); never in a busy spin.
            cv_.wait(lock,[&]{return exit_||(pending_&&requested_!=handled);});
            if(exit_)return;
            if(!pending_||requested_==handled)continue;
            auto value=std::move(pending_);pending_.reset();busy_=true;
            lock.unlock();
            bool ok=true;
            try{saver_(*value);}catch(...){ok=false;}
            lock.lock();
            busy_=false;
            if(!ok)pending_=std::move(value);
            ++completed_;
            ++handled;
            cv_.notify_all();
        }
    }
    Saver saver_;
    std::function<void()> thread_begin_,thread_end_;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::shared_ptr<const T> pending_;
    bool busy_{},exit_{},started_{};
    unsigned requested_{};
    unsigned completed_{};
};
}
