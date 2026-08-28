@ECHO OFF
REM Local Windows build helper: sets env and runs deploy\build_windows.bat
REM Usage:  build_windows_local         (incremental)
REM         build_windows_local clean   (wipe build dir first)
REM Qt location can be overridden by setting QT_ROOT_DIR before calling.

setlocal
cd /d %~dp0..

REM Load MSVC environment from VS 2022 (any edition, incl. Build Tools)
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist %VSWHERE% (
    echo vswhere.exe not found - is Visual Studio 2022 installed?
    exit /b 1
)
set VS2022_DIR=
for /f "usebackq delims=" %%i in (`%VSWHERE% -version "[17.0,18.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath -latest`) do set VS2022_DIR=%%i
if "%VS2022_DIR%"=="" (
    echo VS 2022 with C++ toolset not found
    exit /b 1
)
echo Using MSVC from %VS2022_DIR%
call "%VS2022_DIR%\VC\Auxiliary\Build\vcvars64.bat"
if %errorlevel% neq 0 exit /b %errorlevel%

where ninja >nul 2>nul
if %errorlevel% neq 0 (
    echo ninja not found in PATH - install it: winget install Ninja-build.Ninja
    exit /b 1
)

if "%QT_ROOT_DIR%"=="" set QT_ROOT_DIR=C:\Users\Happy\Desktop\Qt

set CMAKE_GENERATOR=Ninja
set BUILD_ARCH=64
set QT_BIN_DIR=%QT_ROOT_DIR%\6.10.1\msvc2022_64\bin
set QIF_BIN_DIR=%QT_ROOT_DIR%\Tools\QtInstallerFramework\4.7\bin
set WIX_BIN_DIR=%USERPROFILE%\.dotnet\tools

echo QT_BIN_DIR=%QT_BIN_DIR%
echo QIF_BIN_DIR=%QIF_BIN_DIR%

if /i "%1"=="clean" (
    echo Wiping deploy\build_64 ...
    rmdir /Q /S deploy\build_64
)

call deploy\build_windows.bat
endlocal
