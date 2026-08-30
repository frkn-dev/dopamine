@ECHO OFF
REM Builds tunnel.dll with AWG 3.1 support from amnezia-vpn/amneziawg-windows (master)
REM and installs it into client\3rd-prebuilt\deploy-prebuilt\windows\x64\
REM Self-contained: upstream build.cmd downloads its own Go + llvm-mingw toolchain.
REM Requires: git, curl, tar (all present on Windows 10+).

setlocal
cd /d %~dp0..

REM Make sure bsdtar (System32) wins over GNU tar from Git for Windows:
REM GNU tar cannot read zip archives and breaks the upstream build.cmd.
set "PATH=C:\Windows\System32;%PATH%"
where tar
tar --version | findstr /i "bsdtar libarchive" >NUL
if errorlevel 1 (
  echo WARNING: tar does not look like bsdtar, zip extraction may fail
)

set WORK=%TEMP%\amneziawg-windows
echo Using work dir %WORK%
if exist "%WORK%" rmdir /Q /S "%WORK%"

echo Cloning amneziawg-windows ...
git clone --depth 1 https://github.com/amnezia-vpn/amneziawg-windows "%WORK%"
if errorlevel 1 goto fail

REM Move the ring log (log.bin) from Program Files\AmneziaWG to Program Files\Dopamine
echo Patching ring log directory AmneziaWG -^> Dopamine ...
powershell -NoProfile -Command "(Get-Content '%WORK%\conf\path_windows.go' -Raw) -replace 'AmneziaWG', 'Dopamine' | Set-Content '%WORK%\conf\path_windows.go' -NoNewline"
if errorlevel 1 goto fail
findstr /C:"Dopamine" "%WORK%\conf\path_windows.go" >NUL
if errorlevel 1 goto fail

cd /d "%WORK%"
echo Building (downloads Go + llvm-mingw on first run, takes a while) ...
call build.cmd
if errorlevel 1 goto fail

if not exist "%WORK%\x64\tunnel.dll" goto fail

set DEST=%~dp0..\client\3rd-prebuilt\deploy-prebuilt\windows\x64
copy /Y "%WORK%\x64\tunnel.dll" "%DEST%\tunnel.dll"
if errorlevel 1 goto fail

REM Refresh the checksum sidecar
for /f %%a in ('CertUtil -hashfile "%DEST%\tunnel.dll" SHA256 ^| findstr /r "^[0-9a-f][0-9a-f]*$"') do echo %%a> "%DEST%\tunnel.dll.sha256"

echo.
echo Installed new tunnel.dll to %DEST%
echo Verify AWG 3.1 support:
powershell -command "Select-String -Path '%DEST%\tunnel.dll' -Pattern 'random_trailers' -Encoding default -List | Select-Object -First 1"
exit /b 0

:fail
echo tunnel.dll build failed
exit /b 1
