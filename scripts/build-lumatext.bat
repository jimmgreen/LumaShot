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
if not defined LUMATEXT_SOURCE_DIR set "LUMATEXT_SOURCE_DIR=%~dp0..\..\lumatext"
cmake -S "%LUMATEXT_SOURCE_DIR%" -B "%~dp0..\build\lumatext-upstream" -G Ninja -DCMAKE_BUILD_TYPE=Release -DLUMATEXT_BUILD_SHARED=ON -DLUMATEXT_BUILD_STATIC=ON -DLUMATEXT_BUILD_SAMPLES=OFF -DLUMATEXT_BUILD_TESTS=ON %*
if errorlevel 1 exit /b 1
cmake --build "%~dp0..\build\lumatext-upstream" --parallel 4
if errorlevel 1 exit /b 1
ctest --test-dir "%~dp0..\build\lumatext-upstream" --output-on-failure
exit /b %errorlevel%
