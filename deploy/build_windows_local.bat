@ECHO OFF
REM Local Windows build helper: sets env and runs deploy\build_windows.bat
REM Usage:  build_windows_local         (incremental)
REM         build_windows_local clean   (wipe build dir first)
REM Qt location can be overridden: set QT_ROOT_DIR=... before calling.
REM VS2022 location can be overridden: set VS2022_DIR=... before calling.

setlocal
cd /d %~dp0..

if "%VS2022_DIR%"=="" set VS2022_DIR=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
if not exist "%VS2022_DIR%\VC\Auxiliary\Build\vcvars64.bat" goto novs
echo Using MSVC from %VS2022_DIR%
call "%VS2022_DIR%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1

where ninja >nul 2>nul
if errorlevel 1 goto noninja

if "%QT_ROOT_DIR%"=="" set QT_ROOT_DIR=C:\Users\Happy\Desktop\Qt

set CMAKE_GENERATOR=Ninja
set BUILD_ARCH=64
set QT_BIN_DIR=%QT_ROOT_DIR%\6.10.1\msvc2022_64\bin
set QIF_BIN_DIR=%QT_ROOT_DIR%\Tools\QtInstallerFramework\4.7\bin
set WIX_BIN_DIR=%USERPROFILE%\.dotnet\tools

echo QT_BIN_DIR=%QT_BIN_DIR%
echo QIF_BIN_DIR=%QIF_BIN_DIR%

if /i "%1"=="clean" rmdir /Q /S deploy\build_64

call deploy\build_windows.bat
exit /b %errorlevel%

:novs
echo VS 2022 Build Tools not found at %VS2022_DIR%
echo Set VS2022_DIR to your VS2022 installation and retry.
exit /b 1

:noninja
echo ninja not found in PATH - install it: winget install Ninja-build.Ninja
exit /b 1
