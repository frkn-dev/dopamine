# Установка MSI на вин-боксе через SYSTEM-задачу (ssh не даёт elevation).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File deploy\install_msi_win.ps1
#   powershell ... -File deploy\install_msi_win.ps1 -Msi C:\path\to\other.msi
#
# Без -Msi берётся самый свежий MSI из deploy\build_64: зашитая версия в пути
# устаревает на каждом релизе и молча ставит старый билд.
[CmdletBinding()]
param(
    [string]$Msi = ''
)

$ErrorActionPreference = 'Stop'

if (-not $Msi) {
    $buildDir = Join-Path (Split-Path -Parent $PSScriptRoot) 'deploy\build_64'
    $Msi = Get-ChildItem $buildDir -Filter 'Dopamine-*-win64.msi' -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1 -ExpandProperty FullName
    if (-not $Msi) { throw "No MSI in $buildDir" }
}

$log = 'C:\setup\msi_install.log'

if (-not (Test-Path $Msi)) { throw "MSI not found: $Msi" }
Write-Output ('MSI: ' + $Msi)
Write-Output ('     ' + (Get-Item $Msi).Length + ' bytes  ' + (Get-Item $Msi).LastWriteTime)

$proc = Start-Process -FilePath 'msiexec.exe' -ArgumentList @('/i', "`"$Msi`"", '/qn', '/norestart', '/l*v', "`"$log`"") -Wait -PassThru
Write-Output ('msiexec exit: ' + $proc.ExitCode)

$svc = Get-Service -Name 'dopamine-service' -ErrorAction SilentlyContinue
if ($svc) { Write-Output ('service: ' + $svc.Status) }

$exe = 'C:\Program Files\Dopamine\Dopamine.exe'
if (Test-Path $exe) {
    $i = (Get-Item $exe).VersionInfo
    Write-Output ('app: ' + $i.FileVersion + '  ' + (Get-Item $exe).LastWriteTime)
} else {
    Write-Output 'app: NOT FOUND'
}

exit $proc.ExitCode
