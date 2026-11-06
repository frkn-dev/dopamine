# SYSTEM-задача: перенести dotnet-tools (WiX) из профиля пользователя в профиль
# SYSTEM и собрать MSI. Запускается через run_windows_admin_task.ps1.
#
# Зачем: ssh на винде не даёт elevation, а dotnet SDK + wix.exe стоят в профиле
# пользователя. Сборка под SYSTEM падала на "WiX CLI (wix.exe) was not found",
# потому что искала его в C:\Windows\system32\config\systemprofile\.dotnet\tools.

$ErrorActionPreference = 'Stop'
$log = 'C:\setup\build_msi.log'
$repo = 'C:\build\dopamine'
$srcTools = Join-Path $env:USERPROFILE '.dotnet\tools'

if (-not (Test-Path (Join-Path $srcTools 'wix.exe'))) {
    # SYSTEM: ищем профиль пользователя, где реально лежит wix.exe
    $donor = Get-ChildItem 'C:\Users' -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { Join-Path $_.FullName '.dotnet' } |
        Where-Object { Test-Path (Join-Path $_ 'tools\wix.exe') } |
        Select-Object -First 1
    if ($donor) {
        Write-Output "copying dotnet tools from $donor"
        $target = Join-Path $env:USERPROFILE '.dotnet'
        New-Item -ItemType Directory -Force -Path $target | Out-Null
        Copy-Item -Path (Join-Path $donor '*') -Destination $target -Recurse -Force
    } else {
        Write-Output 'no wix.exe found in any user profile'
    }
}

$wixTools = Join-Path $env:USERPROFILE '.dotnet\tools'
Write-Output ('wix: ' + (Test-Path (Join-Path $wixTools 'wix.exe')))

# CPackWIX ищет 'wix' в PATH — у SYSTEM профильный tools-каталог в PATH не входит
$env:PATH = $wixTools + ';' + $env:PATH

Remove-Item $log -ErrorAction SilentlyContinue
Get-ChildItem -Path $repo -Filter '*.msi' -File -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue

Set-Location (Join-Path $repo 'deploy')
& cmd.exe /c "build_windows_local.bat > `"$log`" 2>&1"
$code = $LASTEXITCODE
Write-Output ("build exit: {0}" -f $code)

if (Test-Path (Join-Path $repo 'Dopamine_x64.msi')) {
    $msi = Get-Item (Join-Path $repo 'Dopamine_x64.msi')
    Write-Output ("MSI: {0} bytes  {1}" -f $msi.Length, $msi.LastWriteTime)
} else {
    Write-Output 'MSI: not produced'
}
exit $code
