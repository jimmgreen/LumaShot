#pragma once
#include <dlgs.h>
struct PreviewDialogProbe {Ui* ui{};bool accept{},observed{};};
UINT_PTR CALLBACK PreviewDialogHook(HWND hook,UINT message,WPARAM,LPARAM data){
    if(message==WM_INITDIALOG){const auto* options=reinterpret_cast<OPENFILENAMEW*>(data);SetWindowLongPtrW(hook,GWLP_USERDATA,options->lCustData);}
    if(message==WM_NOTIFY&&reinterpret_cast<NMHDR*>(data)->code==CDN_INITDONE){
        SetTimer(hook,91,150,[](HWND h,UINT,UINT_PTR timer,DWORD){
            KillTimer(h,timer);auto& probe=*reinterpret_cast<PreviewDialogProbe*>(GetWindowLongPtrW(h,GWLP_USERDATA));auto& u=*probe.ui;
            probe.observed=true;
            Expect(!IsWindowEnabled(u.window)&&!IsWindowVisible(u.video),"save dialog disables preview owner and hides topmost video");
            u.LayoutVideo();SendMessageW(u.window,WM_MOVE,0,0);
            Expect(!IsWindowVisible(u.video),"nested preview layout cannot cover the modal dialog");
            PostMessageW(GetParent(h),WM_COMMAND,probe.accept?IDOK:IDCANCEL,0);
        });
    }
    return 0;
}
void PreviewDialogTest(Ui& u){
    const auto directory=std::filesystem::current_path()/L"preview-dialog-fixture";std::filesystem::create_directories(directory);
    for(bool accept:{false,true}){
        PreviewDialogProbe probe{&u,accept,false};const auto destination=directory/(accept?L"accepted.mp4":L"canceled.mp4");
        wchar_t file[32768]{};wcscpy_s(file,destination.c_str());OPENFILENAMEW dialog{sizeof(dialog)};
        dialog.hwndOwner=u.window;dialog.lpstrFile=file;dialog.nMaxFile=32768;dialog.lpstrInitialDir=directory.c_str();dialog.lpstrFilter=L"MP4\0*.mp4\0\0";dialog.lpstrDefExt=L"mp4";
        dialog.Flags=OFN_EXPLORER|OFN_ENABLEHOOK|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_DONTADDTORECENT;dialog.lpfnHook=PreviewDialogHook;dialog.lCustData=reinterpret_cast<LPARAM>(&probe);
        const bool accepted=GetSaveFileNameW(&dialog)!=FALSE;
        Expect(probe.observed&&accepted==accept,"real save dialog accepts and cancels correctly");
        Expect(IsWindowEnabled(u.window)&&IsWindowVisible(u.video),"preview video returns after save dialog closes");
        Expect(!u.playing,"dialog leaves playback paused");
    }
    // The shell can retain a directory handle briefly after its dialog closes.
    std::error_code cleanup_error;std::filesystem::remove(directory,cleanup_error);
}
