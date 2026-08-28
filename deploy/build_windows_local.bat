@ECHO OFF
REM Local Windows build helper: sets env and runs deploy\build_windows.bat
REM Usage:  winbuild         (incremental)
REM         winbuild clean   (wipe build dir first)
REM Qt location can be overridden by setting QT_ROOT_DIR before calling.

setlocal
cd /d %~dp0..

if "%QT_ROOT_DIR%"=="" set QT_ROOT_DIR=C:\Users\Happy\Desktop\Qt

set CMAKE_GENERATOR=Visual Studio 17 2022
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
