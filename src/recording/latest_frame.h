#pragma once
#include <utility>

namespace lumashot::recording {
// Capture frames are leases: release superseded frames while draining, and keep
// the newest lease alive until the caller has submitted its texture copy.
template<class Acquire,class Validate,class Consume>
void ConsumeLatestFrame(Acquire&& acquire,Validate&& validate,Consume&& consume){
    using Frame=decltype(acquire());
    struct Close {
        Frame& frame;
        ~Close(){try{if(frame)frame.Close();}catch(...){}}
    };
    Frame newest{nullptr};Close newest_close{newest};
    for(;;){
        auto frame=acquire();if(!frame)break;
        Close current_close{frame};validate(frame);
        if(newest)newest.Close();newest=std::move(frame);
    }
    if(newest)consume(newest);
}
}
