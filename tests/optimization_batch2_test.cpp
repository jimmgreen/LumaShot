#include "app/deferred_writer.h"
#include "recording/progress_file.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>
using namespace lumashot;
namespace {
int failures{},checks{};
void Expect(bool value,const char* text){++checks;if(!value){++failures;std::cout<<"FAIL "<<text<<'\n';}}
struct Latch {
    std::mutex mutex;std::condition_variable cv;bool value{};
    void Wait(){std::unique_lock lock(mutex);cv.wait(lock,[&]{return value;});}
    void Set(){std::lock_guard lock(mutex);value=true;cv.notify_all();}
};
void Writer(){
    {
        Latch entered,gate;std::vector<int> saved;std::mutex lock;
        DeferredWriter<int> writer{[&](int value){entered.Set();gate.Wait();std::lock_guard guard(lock);saved.push_back(value);}};
        writer.Request(1);entered.Wait(); // worker is busy saving the first value
        writer.Request(2);gate.Set();
        Expect(writer.Flush(),"gated background saves complete");
        {std::lock_guard guard(lock);Expect(saved==std::vector<int>{1,2},"last-write-wins order preserved with coalescing");}
    }
    {
        DeferredWriter<int> writer{[](int){throw std::runtime_error("synthetic settings failure");}};
        writer.Request(5);
        Expect(!writer.Flush(),"save failure is reported synchronously to the caller");
        writer.DropPending();
        writer.Request(6);writer.DropPending();
        Expect(writer.Flush(),"dropped pending values are not retried");
    }
    {
        std::vector<int> saved;std::mutex lock;
        {
            DeferredWriter<int> writer{[&](int value){std::lock_guard guard(lock);saved.push_back(value);}};
            writer.Request(9); // destructor must flush without an explicit Flush
        }
        std::lock_guard guard(lock);Expect(saved==std::vector<int>{9},"destructor flushes the pending value before joining");
    }
    {
        DeferredWriter<int> idle;
        Expect(idle.Flush(),"flushing a never-used writer succeeds");
        DeferredWriter<int> writer{[](int){}};
        writer.Request(1);writer.Request(2);writer.Request(3);
        Expect(writer.Flush(),"rapid requests keep the writer consistent");
    }
}
void Write(const std::filesystem::path& path,const std::string& text){std::ofstream out(path,std::ios::binary);out<<text;}
void Append(const std::filesystem::path& path,const std::string& text){std::ofstream out(path,std::ios::binary|std::ios::app);out<<text;}
void Progress(){
    const auto file=std::filesystem::temp_directory_path()/(L"luma-progress-"+std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count())+L".txt");
    Write(file,"out_time_us=100\nout_time_us=2\n");
    {recording::ProgressTail tail;const auto first=tail.Poll(file,"out_time_us=");
        Expect(first&&*first==2,"first poll consumes the whole file and reports the last value");
        Expect(!tail.Poll(file,"out_time_us="),"unchanged files poll as empty");}
    Append(file,"out_time_us=350\r\nout_time_us=9");
    {recording::ProgressTail tail;const auto second=tail.Poll(file,"out_time_us=");
        Expect(second&&*second==350,"appended bytes are parsed and CRLF is tolerated");}
    Append(file,"99\n");
    {recording::ProgressTail tail;const auto third=tail.Poll(file,"out_time_us=");
        Expect(third&&*third==999,"a line split across appends parses once completed");}
    Append(file,"frame=77\n");
    {recording::ProgressTail tail;const auto frame=tail.Poll(file,"frame=");
        Expect(frame&&*frame==77,"frame prefix polls independently");}
    Write(file,"out_time_us=5");
    {recording::ProgressTail tail;Expect(!tail.Poll(file,"out_time_us="),"a shrunken file never rewinds the offset");}
    std::error_code ignored;std::filesystem::remove(file,ignored);
}
}
int main(int argc,char** argv){
    const std::string mode=argc>1?argv[1]:"";
    try{if(mode!="--progress")Writer();if(mode!="--writer")Progress();}
    catch(const std::exception& e){++failures;std::cout<<"FAIL exception: "<<e.what()<<'\n';}
    std::cout<<"checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
