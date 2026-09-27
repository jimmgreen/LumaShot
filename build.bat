@echo off
setlocal
set "LUMASHOT_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%LUMASHOT_VSWHERE%" exit /b 1
for /f "usebackq delims=" %%i in (`"%LUMASHOT_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "LUMASHOT_VS=%%i"
if not defined LUMASHOT_VS exit /b 1
call "%LUMASHOT_VS%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
chcp 65001 >nul
set "VSLANG=1033"
cmake -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_BUILD_TYPE=Release %*
if errorlevel 1 exit /b 1
cmake --build "%~dp0build"
exit /b %errorlevel%
