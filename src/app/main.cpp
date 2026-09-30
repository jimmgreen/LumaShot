#include "app/application.h"
#include "app/launch_options.h"
#include "app/settings_process.h"
#include "app/translation_settings.h"
#include "export/png.h"
#include "ui/themed_message.h"
#include <shellapi.h>
#include <commctrl.h>

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    // A portable diagnostic build uses the same entry point, settings, tray and
    // hotkey path. Opt in locally without changing normal release behavior.
    wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);
    const auto folder=std::filesystem::path(module).parent_path();
    if(std::filesystem::exists(folder/L"diagnostics.enabled"))
        SetEnvironmentVariableW(L"LUMASHOT_TRACE",(folder/L"interaction-trace.csv").c_str());
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(com))return 1;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
    int count{};LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&count);
    const bool client_mode=count>1&&(std::wstring(args[1])==L"--capture-result"||std::wstring(args[1])==L"--capture-result-demo");
    int exit_code{};
    HANDLE instance{};
    try {
        if(count==2&&std::wstring(args[1])==L"--capture-ipc-demo"){lumashot::Application application;exit_code=application.RunIpcDemo();}
        else if(client_mode) {
            if(count!=3)throw std::runtime_error("Expected capture output path");
            lumashot::Application application;
            exit_code=application.RunForClient(args[2],std::wstring(args[1])==L"--capture-result-demo");
        }
        else if(count>1&&std::wstring(args[1])==L"--settings-worker"){exit_code=lumashot::SettingsWorkerMain(count,args);}
        else if(count>1&&std::wstring(args[1])==L"--translation-settings"){exit_code=lumashot::TranslationSettingsMain(count>2&&std::wstring(args[2])==L"--dark");}
        else if(count==3&&(std::wstring(args[1])==L"--render-demo"||std::wstring(args[1])==L"--render-demo-dark")) {
            lumashot::Renderer renderer;
            lumashot::SavePng(renderer.Demo(true,std::wstring(args[1])==L"--render-demo-dark"),args[2]);
        } else {
            const bool demo=count>1&&std::wstring(args[1])==L"--demo";
            instance=CreateMutexW(nullptr,FALSE,demo?L"Local\\LumaShot.DemoInstance":L"Local\\LumaShot.SingleInstance");
            if(GetLastError()==ERROR_ALREADY_EXISTS) {
                HWND existing=FindWindowW(L"LumaShot.Host",L"LumaShot");
                if(existing&&lumashot::ActivateExistingOnLaunch(count>1?args[1]:L""))PostMessageW(existing,lumashot::LaunchCommandMessage,1,0);
            }else {
                lumashot::Application application;
                exit_code=application.Run(lumashot::CaptureOnLaunch(count>1?args[1]:L""),count>1&&std::wstring(args[1])==L"--demo");
            }
        }
    }catch(const std::exception& error) {
        if(!client_mode&&(count<2||std::wstring(args[1])!=L"--render-demo")){
            const int length=MultiByteToWideChar(CP_UTF8,0,error.what(),-1,nullptr,0);
            std::wstring message(static_cast<size_t>(std::max(1,length)),L'\0');
            if(length>0){MultiByteToWideChar(CP_UTF8,0,error.what(),-1,message.data(),length);message.pop_back();}
            else message=L"无法启动 LumaShot。";
            lumashot::ui::ShowThemedMessage(nullptr,false,L"LumaShot",message);
        }
        exit_code=1;
    }
    if(instance)CloseHandle(instance);if(args)LocalFree(args);CoUninitialize();return exit_code;
}

