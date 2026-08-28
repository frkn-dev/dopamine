@ECHO OFF
REM Builds zlib 1.3.1 (static, x64) and installs headers+lib into client\3rd-prebuilt\3rd-prebuilt\zlib\windows
REM Needed once per machine: client code includes <zlib.h> (gzipDecompress), Windows has no system zlib.

setlocal
cd /d %~dp0..

if "%VS2022_DIR%"=="" set VS2022_DIR=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
if not exist "%VS2022_DIR%\VC\Auxiliary\Build\vcvars64.bat" goto novs
call "%VS2022_DIR%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1

where ninja >nul 2>nul
if errorlevel 1 goto noninja

set WORK=%TEMP%\zlib-frkn
rmdir /Q /S "%WORK%" 2>nul
mkdir "%WORK%"
cd /d "%WORK%"

echo Downloading zlib 1.3.1 ...
curl -L -o zlib.tar.gz https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
if errorlevel 1 goto fail
tar -xf zlib.tar.gz
if errorlevel 1 goto fail

cmake -S zlib-1.3.1 -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto fail
cmake --build build --target zlibstatic
if errorlevel 1 goto fail

set DEST=%~dp0..\client\3rd-prebuilt\3rd-prebuilt\zlib\windows
mkdir "%DEST%\include" 2>nul
mkdir "%DEST%\win64" 2>nul
copy /Y zlib-1.3.1\zlib.h "%DEST%\include\"
copy /Y build\zconf.h "%DEST%\include\"
copy /Y build\zlibstatic.lib "%DEST%\win64\"

echo Installed zlib to %DEST%
exit /b 0

:novs
echo VS 2022 Build Tools not found at %VS2022_DIR%
exit /b 1

:noninja
echo ninja not found in PATH - install it: winget install Ninja-build.Ninja
exit /b 1

:fail
echo zlib build failed
exit /b 1
