@echo off
REM Build MSI as the logged-in user (SYSTEM has no dotnet/wix in profile).
cd /d C:\build\dopamine\deploy
build_windows_local.bat > C:\setup\build_msi.log 2>&1
echo %ERRORLEVEL% > C:\setup\DopamineBuild.exit
