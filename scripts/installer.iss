#ifndef PayloadDir
  #define PayloadDir "..\dist\LumaShot-setup-payload"
#endif
#ifndef OutputDirPath
  #define OutputDirPath "..\dist"
#endif
#ifndef AppVersion
  #define AppVersion "0.2.0"
#endif

[Setup]
AppId={{7C856859-ABF1-40C1-9E85-F5E658390CB3}
AppName=LumaShot
AppVersion={#AppVersion}
AppPublisher=LumaShot
DefaultDirName={localappdata}\Programs\LumaShot
DefaultGroupName=LumaShot
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDirPath}
OutputBaseFilename=LumaShot-Setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\LumaShot.exe
CloseApplications=force
CloseApplicationsFilter=*.exe,*.dll
RestartApplications=no
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Types]
Name: "full"; Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "core"; Description: "LumaShot"; Types: full custom; Flags: fixed
Name: "ocr"; Description: "Offline text recognition (OCR)"; Types: full

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Excludes: "docs\*.csv,docs\*.txt,docs\*.json,ocr\*,lumashot_ocr_worker.exe,onnxruntime.dll"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PayloadDir}\ocr\*"; DestDir: "{app}\ocr"; Components: ocr; Flags: ignoreversion
Source: "{#PayloadDir}\lumashot_ocr_worker.exe"; DestDir: "{app}"; Components: ocr; Flags: ignoreversion
Source: "{#PayloadDir}\onnxruntime.dll"; DestDir: "{app}"; Components: ocr; Flags: ignoreversion

; Upgrade deselection removes only this distribution's known OCR files.
[InstallDelete]
Type: files; Name: "{app}\lumashot_ocr_worker.exe"; Check: not WizardIsComponentSelected('ocr')
Type: files; Name: "{app}\onnxruntime.dll"; Check: not WizardIsComponentSelected('ocr')
Type: files; Name: "{app}\ocr\det.onnx"; Check: not WizardIsComponentSelected('ocr')
Type: files; Name: "{app}\ocr\rec.onnx"; Check: not WizardIsComponentSelected('ocr')
Type: files; Name: "{app}\ocr\dictionary.txt"; Check: not WizardIsComponentSelected('ocr')
Type: files; Name: "{app}\ocr\LICENSE"; Check: not WizardIsComponentSelected('ocr')
Type: files; Name: "{app}\ocr\LICENSE-PaddleOCR.txt"; Check: not WizardIsComponentSelected('ocr')
Type: files; Name: "{app}\ocr\manifest.json"; Check: not WizardIsComponentSelected('ocr')
Type: files; Name: "{app}\ocr\runtime-build.json"; Check: not WizardIsComponentSelected('ocr')
Type: files; Name: "{app}\ocr\ThirdPartyNotices.txt"; Check: not WizardIsComponentSelected('ocr')
Type: dirifempty; Name: "{app}\ocr"; Check: not WizardIsComponentSelected('ocr')

[Icons]
Name: "{group}\LumaShot"; Filename: "{app}\LumaShot.exe"; WorkingDir: "{app}"
Name: "{userdesktop}\LumaShot"; Filename: "{app}\LumaShot.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueName: "LumaShot"; Flags: uninsdeletevalue

[Run]
Filename: "{app}\LumaShot.exe"; Parameters: "--background"; WorkingDir: "{app}"; Flags: nowait runasoriginaluser

; Start the tray process after successful installation, including silent installs.
; App manages per-user login startup from settings; uninstall removes only its Run value.
; Preferences and pin session cache remain outside {app}.

[Code]
const
  ProcessQueryLimitedInformation = $1000;
  ProcessSynchronize = $100000;
  WaitObject0 = 0;
  CloseMessage = $0010;

function GetWindowThreadProcessId(Window: HWND; var ProcessId: LongWord): LongWord;
  external 'GetWindowThreadProcessId@user32.dll stdcall';
function OpenProcess(Access: LongWord; Inherit: Boolean; ProcessId: LongWord): THandle;
  external 'OpenProcess@kernel32.dll stdcall';
function QueryFullProcessImageName(Process: THandle; Flags: LongWord; Name: string; var Size: LongWord): Boolean;
  external 'QueryFullProcessImageNameW@kernel32.dll stdcall';
function WaitForSingleObject(Handle: THandle; Milliseconds: LongWord): LongWord;
  external 'WaitForSingleObject@kernel32.dll stdcall';
function CloseHandle(Handle: THandle): Boolean;
  external 'CloseHandle@kernel32.dll stdcall';

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Window: HWND;
  ProcessId, Size: LongWord;
  Process: THandle;
  ImageName, TargetName: String;
  Attempt: Integer;
begin
  Result := '';
  Window := FindWindowByClassName('LumaShot.Host');
  if Window = 0 then exit;
  GetWindowThreadProcessId(Window, ProcessId);
  Process := OpenProcess(ProcessQueryLimitedInformation or ProcessSynchronize, False, ProcessId);
  if Process = 0 then begin
    Result := 'Unable to access the running LumaShot. Close it and retry.';
    exit;
  end;
  try
    Size := 32768;
    SetLength(ImageName, Size);
    if not QueryFullProcessImageName(Process, 0, ImageName, Size) then begin
      Result := 'Unable to identify the running LumaShot. Close it and retry.';
      exit;
    end;
    SetLength(ImageName, Size);
    TargetName := ExpandConstant('{app}\LumaShot.exe');
    { Only stop the installation being updated, never an unrelated portable copy. }
    if CompareText(ImageName, TargetName) <> 0 then exit;
    Log('Closing installed LumaShot before replacing files: ' + ImageName);
    PostMessage(Window, CloseMessage, 0, 0);
    for Attempt := 1 to 150 do begin
      if WaitForSingleObject(Process, 0) = WaitObject0 then exit;
      Sleep(100);
    end;
    Result := 'LumaShot is still closing. Please retry in a moment.';
  finally
    CloseHandle(Process);
  end;
end;