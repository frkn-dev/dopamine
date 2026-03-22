sc stop AmneziaWGTunnel$AmneziaVPN
sc delete AmneziaWGTunnel$AmneziaVPN
taskkill /IM "FRKN-service.exe" /F
taskkill /IM "FRKN.exe" /F
exit /b 0
