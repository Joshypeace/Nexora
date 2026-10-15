@echo off
rem Builds Nexora from a plain shell: loads the MSVC x64 environment, configures, builds,
rem runs the headless tests and deploys the Qt runtime to dist\Nexora.
rem
rem   tools\build.cmd [release|debug] [package]
rem
rem "package" also creates dist\Nexora-<version>-portable-x64.zip and, when WiX Toolset v3
rem is installed, the per-user installer dist\Nexora-<version>-x64.msi.
setlocal
set PRESET=%1
if "%PRESET%"=="" set PRESET=release
set PACKAGE=%2

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

if /i not "%PACKAGE%"=="package" exit /b 0
cmake --build --preset %PRESET% --target portable || exit /b 1
if exist "%WIX%bin\candle.exe" goto installer
if exist "%ProgramFiles(x86)%\WiX Toolset v3.14\bin\candle.exe" goto installer
if exist "%ProgramFiles(x86)%\WiX Toolset v3.11\bin\candle.exe" goto installer
echo WiX Toolset v3 not found: skipped the MSI installer.
exit /b 0
:installer
cmake --build --preset %PRESET% --target installer || exit /b 1
echo Packages are in dist\
