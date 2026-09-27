#include "app/application.h"
#include <commctrl.h>

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
#ifdef LUMASHOT_PROFILE
    wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);
#ifdef LUMASHOT_PROFILE_REAL
    const auto trace=std::filesystem::path(module).parent_path()/L"real-interaction-trace.csv";
#else
    const auto trace=std::filesystem::path(module).parent_path()/L"interaction-trace.csv";
#endif
    SetEnvironmentVariableW(L"LUMASHOT_TRACE",trace.c_str());
#endif
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 2;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};
    InitCommonControlsEx(&controls);
    int result=1;
    try {lumashot::Application app;
#ifdef LUMASHOT_PROFILE_REAL
        result=app.Run(true,false,true);
#else
        result=app.Run(false,true);
#endif
    }
    catch(const std::exception& e){MessageBoxA(nullptr,e.what(),"LumaShot interactive test",MB_OK);}
    CoUninitialize();return result;
}
