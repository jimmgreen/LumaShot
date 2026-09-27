#pragma once
#include <windows.h>
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <algorithm>
#include <cstring>

namespace lumashot {
// An idle host allocates no thread, timer, pipe or frame buffer for IPC.
inline constexpr ULONG_PTR CaptureRequestTag=0x4c534331, CaptureCancelTag=0x4c534332;
inline UINT CaptureVersionMessage(){static const UINT id=RegisterWindowMessageW(L"LumaShot.CaptureIPC.Version");return id;}
inline std::wstring CapturePayload(const COPYDATASTRUCT* data){
    if(!data||!data->lpData||data->cbData<2||data->cbData>65536||data->cbData%sizeof(wchar_t))return {};
    std::wstring text(data->cbData/sizeof(wchar_t),L'\0');
    std::memcpy(text.data(),data->lpData,data->cbData);
    if(text.back()!=L'\0')return {};text.pop_back();
    if(text.find(L'\0')!=std::wstring::npos)return {};return text;
}
struct CaptureIpcSession {
    std::wstring token;
    std::filesystem::path output;
    std::array<HANDLE,3> events{};
    HANDLE caller{};
    ~CaptureIpcSession(){for(auto event:events)if(event)CloseHandle(event);if(caller)CloseHandle(caller);}
    CaptureIpcSession()=default;
    CaptureIpcSession(const CaptureIpcSession&)=delete;
    CaptureIpcSession& operator=(const CaptureIpcSession&)=delete;
    void Complete(int status){SetEvent(events[status==0?0:status==2?1:2]);}
    static std::unique_ptr<CaptureIpcSession> Open(const std::wstring& text){
        if(!text.starts_with(L"1\n"))return {};
        const auto split=text.find(L'\n',2);if(split!=34)return {};
        auto session=std::make_unique<CaptureIpcSession>();session->token=text.substr(2,32);
        if(!std::all_of(session->token.begin(),session->token.end(),[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');}))return {};
        const auto end=text.find(L'\n',35);if(end==std::wstring::npos||end==35||end>45)return {};
        unsigned long long pid=0;
        for(size_t i=35;i<end;++i){if(text[i]<L'0'||text[i]>L'9')return {};pid=pid*10+text[i]-L'0';}
        if(!pid||pid>MAXDWORD)return {};
        const auto path=text.substr(end+1);if(path.find_first_of(L"\r\n")!=std::wstring::npos)return {};
        session->output=path;
        std::error_code ec;
        if(!session->output.is_absolute()||session->output.extension()!=L".png"||!std::filesystem::is_directory(session->output.parent_path(),ec)||ec||std::filesystem::exists(session->output,ec)||ec)return {};
        DWORD caller_session{},own_session{};
        if(!ProcessIdToSessionId(static_cast<DWORD>(pid),&caller_session)||!ProcessIdToSessionId(GetCurrentProcessId(),&own_session)||caller_session!=own_session)return {};
        session->caller=OpenProcess(SYNCHRONIZE,FALSE,static_cast<DWORD>(pid));
        if(!session->caller||WaitForSingleObject(session->caller,0)!=WAIT_TIMEOUT)return {};
        constexpr const wchar_t* suffix[]={L".ok",L".cancel",L".error"};
        for(size_t i=0;i<session->events.size();++i){
            const auto name=L"Local\\LumaShot.Capture."+session->token+suffix[i];
            session->events[i]=OpenEventW(EVENT_MODIFY_STATE,FALSE,name.c_str());if(!session->events[i])return {};
        }
        return session;
    }
};
}
