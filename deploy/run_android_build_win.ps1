# Обёртка для сборки APK на боксе: подписка берётся из файлов, а не из аргументов.
$ErrorActionPreference = 'Stop'

$env:ANDROID_KEYSTORE_PATH = 'C:\setup\frkn-release-key.jks'
$env:ANDROID_KEYSTORE_KEY_ALIAS = 'frkn-key'
$env:ANDROID_KEYSTORE_KEY_PASS = ((Get-Content 'C:\setup\keystore.pass' -Raw).Trim())

& 'C:\build\dopamine\deploy\build_android_windows.ps1' -Abi arm64-v8a -BuildType release
exit $LASTEXITCODE
