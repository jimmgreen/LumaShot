#pragma once
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace lumashot::recording {
// ffmpeg -progress only ever appends lines. Polling used to re-read the whole
// file every 250ms, which turns a long export quadratic. Track the consumed
// byte offset and parse only newly completed lines instead.
class ProgressTail {
public:
    // Returns the newest value among lines appended since the previous poll
    // that start with prefix; nullopt when no new complete line arrived.
    std::optional<long long> Poll(const std::filesystem::path& file,const char* prefix){
        std::ifstream in(file,std::ios::binary);
        if(!in)return std::nullopt;
        in.seekg(0,std::ios::end);
        const auto end=in.tellg();
        if(end<0||static_cast<unsigned long long>(end)<=offset_)return std::nullopt;
        in.seekg(static_cast<std::streamoff>(offset_));
        std::string chunk(static_cast<size_t>(end-static_cast<std::streamoff>(offset_)),'\0');
        in.read(chunk.data(),static_cast<std::streamsize>(chunk.size()));
        chunk.resize(in.gcount()>0?static_cast<size_t>(in.gcount()):0);
        offset_=static_cast<unsigned long long>(end);
        carry_+=chunk;
        const std::string_view key(prefix);
        std::optional<long long> latest;
        size_t start=0;
        for(;;){
            const auto newline=carry_.find('\n',start);
            if(newline==std::string::npos)break;
            std::string_view line(carry_.data()+start,newline-start);
            start=newline+1;
            if(!line.empty()&&line.back()=='\r')line.remove_suffix(1);
            if(line.starts_with(key)){
                try{latest=std::stoll(std::string(line.substr(key.size())));}catch(const std::exception&){}
            }
        }
        carry_.erase(0,start);
        return latest;
    }
private:
    unsigned long long offset_{};
    std::string carry_;
};
}
