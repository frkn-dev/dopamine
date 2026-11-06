# Ставит WiX dotnet-tool в профиль SYSTEM через dotnet.exe из профиля пользователя,
# проверяет, что wix реально запускается, и только потом упаковывает MSI.

$ErrorActionPreference = 'Continue'
$wixTools = Join-Path $env:USERPROFILE '.dotnet\tools'
$env:PATH = $wixTools + ';' + $env:PATH

function Find-DonorDotnet {
    $candidates = @()
    $candidates += (Get-ChildItem 'C:\Users' -Directory -ErrorAction SilentlyContinue | ForEach-Object {
        Join-Path $_.FullName '.dotnet\dotnet.exe'
    })
    $candidates += 'C:\Program Files\dotnet\dotnet.exe'
    $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}

$wixExe = Join-Path $wixTools 'wix.exe'
$versionOk = $false
if (Test-Path $wixExe) {
    $out = & $wixExe --version 2>&1
    Write-Output ('wix --version -> ' + ($out -join ' '))
    $versionOk = ($LASTEXITCODE -eq 0)
}

if (-not $versionOk) {
    $dotnet = Find-DonorDotnet
    Write-Output ('donor dotnet: ' + $dotnet)
    if ($dotnet) {
        & $dotnet tool install --global wix --add-source https://api.nuget.org/v3/index.json 2>&1 | ForEach-Object { Write-Output $_ }
        if (Test-Path $wixExe) {
            $out = & $wixExe --version 2>&1
            Write-Output ('wix --version after install -> ' + ($out -join ' '))
            $versionOk = ($LASTEXITCODE -eq 0)
        }
    }
}

if (-not $versionOk) {
    Write-Output 'wix is not runnable for SYSTEM - aborting packaging'
    exit 2
}

$repo = 'C:\build\dopamine'
$buildDir = Join-Path $repo 'deploy\build_64'
Get-ChildItem -Path $repo -Filter '*.msi' -File -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue

$cpack = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cpack.exe'
Set-Location $buildDir
& cmd.exe /c "`"$cpack`" -G WIX --config CPackConfig.cmake > C:\setup\cpack.log 2>&1"
Write-Output ('cpack exit: ' + $LASTEXITCODE)

Get-ChildItem -Path $repo -Filter '*.msi' -File -Recurse -ErrorAction SilentlyContinue |
    Select-Object -First 3 |
    ForEach-Object { Write-Output ('MSI: ' + $_.FullName + '  ' + $_.Length + '  ' + $_.LastWriteTime) }
