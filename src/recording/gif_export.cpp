#include "recording/gif_export.h"
#include "recording/gif_optimizer.h"
#include "recording/storage.h"
namespace lumashot::recording {
void ExportGifToFile(const std::filesystem::path& source,const std::filesystem::path& destination,
    const GifOptions& options,std::stop_token stop,const std::function<void(int)>& progress){
    if(stop.stop_requested())throw std::runtime_error("Export canceled");
    ExportDirectory directory(std::filesystem::absolute(source).parent_path());
    const auto output=directory.Path()/L"export.gif";
    ExportGif(source,output,options,stop,[&](int value){progress(value*80/100);});
    OptimizeGif(output,options.loop,stop,[&](int value){progress(80+value*19/100);});
    SaveOutput(output,destination,stop);
    progress(100);
}
}
