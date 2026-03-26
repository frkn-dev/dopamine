sc stop AmneziaWGTunnel$AmneziaVPN
sc delete AmneziaWGTunnel$AmneziaVPN
taskkill /IM "FRKN-service.exe" /F
taskkill /IM "FRKN.exe" /F

rem Register frkn:// URL scheme
reg add "HKCU\Software\Classes\frkn" /ve /d "URL:FRKN Protocol" /f
reg add "HKCU\Software\Classes\frkn" /v "URL Protocol" /d "" /f
reg add "HKCU\Software\Classes\frkn\shell\open\command" /ve /d "\"%~dp0FRKN.exe\" \"%%1\"" /f

exit /b 0
