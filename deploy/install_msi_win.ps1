# Установка MSI на вин-боксе через SYSTEM-задачу (ssh не даёт elevation).
$ErrorActionPreference = 'Stop'
$msi = 'C:\setup\Dopamine-4.8.79-win64.msi'
$log = 'C:\setup\msi_install.log'

if (-not (Test-Path $msi)) { throw "MSI not found: $msi" }
Write-Output ('MSI: ' + (Get-Item $msi).Length + ' bytes  ' + (Get-Item $msi).LastWriteTime)

$proc = Start-Process -FilePath 'msiexec.exe' -ArgumentList @('/i', "`"$msi`"", '/qn', '/norestart', '/l*v', "`"$log`"") -Wait -PassThru
Write-Output ('msiexec exit: ' + $proc.ExitCode)

$svc = Get-Service -Name 'dopamine-service' -ErrorAction SilentlyContinue
if ($svc) { Write-Output ('service: ' + $svc.Status) }

$exe = 'C:\Program Files\Dopamine\Dopamine.exe'
if (Test-Path $exe) { Write-Output ('app: ' + (Get-Item $exe).VersionInfo.FileVersion + '  ' + (Get-Item $exe).LastWriteTime) }
exit $proc.ExitCode
