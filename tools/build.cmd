@echo off
rem Builds Nexora from a plain shell: loads the MSVC x64 environment, then configures,
rem builds and deploys the Qt runtime. Usage: tools\build.cmd [release|debug]
setlocal
set PRESET=%1
if "%PRESET%"=="" set PRESET=release

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (echo Visual Studio with C++ tools not found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

cd /d "%~dp0.."
cmake --preset %PRESET% || exit /b 1
cmake --build --preset %PRESET% || exit /b 1
rem Headless tests only; the UI test opens a real window (run it with: ctest --preset %PRESET% -L ui).
ctest --test-dir out\build\%PRESET% -LE ui --output-on-failure || exit /b 1
cmake --build --preset %PRESET% --target deploy || exit /b 1
echo.
echo Built dist\Nexora\Nexora.exe
