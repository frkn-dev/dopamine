<#
Run deploy\setup_windows_admin.ps1 as SYSTEM through a scheduled task, so it can
be driven from an ssh session (Windows OpenSSH never gives an elevated token and
there is no way to answer a UAC prompt remotely).

  ssh <user>@<win> "powershell -NoProfile -ExecutionPolicy Bypass -File C:\build\dopamine\deploy\run_windows_admin_task.ps1"

Prints the task log and exits with the task's exit code, so the caller sees the
same result an elevated local run would give.
#>
[CmdletBinding()]
param(
    [string]$Script   = (Join-Path $PSScriptRoot 'setup_windows_admin.ps1'),
    [string]$ExtraArgs = '',
    [string]$TaskName = 'DopamineWinAdminSetup',
    [int]$TimeoutMin  = 90
)

$ErrorActionPreference = 'Stop'
function Step($m) { Write-Host "`n=== $m ===" -ForegroundColor Cyan }
function Note($m) { Write-Host "    $m" -ForegroundColor DarkGray }

if (-not (Test-Path $Script)) { throw "Admin script not found: $Script" }

# Keep log/marker/wrapper next to the script: this directory is plain ASCII. The
# previous version used $env:TEMP, whose path contains the Cyrillic account name,
# and Set-Content -Encoding ASCII turned it into "????" - the wrapper then wrote
# nowhere and the caller waited until the timeout with no log to show.
$workDir = Split-Path -Parent $Script
$log     = Join-Path $workDir "$TaskName.log"
$done    = Join-Path $workDir "$TaskName.exit"
$wrapper = Join-Path $workDir "$TaskName.cmd"

Remove-Item $log, $done -ErrorAction SilentlyContinue

# The wrapper runs the real script, captures its exit code and leaves a marker so
# the caller can tell "finished" from "still running".
@"
@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "$Script" $ExtraArgs > "$log" 2>&1
echo %errorlevel% > "$done"
"@ | Set-Content -Path $wrapper -Encoding ASCII

Step "Registering scheduled task $TaskName (SYSTEM, highest)"
# schtasks /delete exits non-zero when the task does not exist yet (first run on
# a fresh box). With $ErrorActionPreference='Stop' that native stderr becomes a
# terminating error, so relax it just for this call.
$prevEap = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& schtasks.exe @('/delete','/tn',$TaskName,'/f') 2>$null | Out-Null
$ErrorActionPreference = $prevEap
# The task target is the wrapper .cmd itself: nesting `cmd /c "..."` inside
# /tr breaks on the quotes that schtasks keeps literally.
$create = @('/create','/tn',$TaskName,'/tr',"`"$wrapper`"",'/sc','once','/st','00:00',
            '/ru','SYSTEM','/rl','HIGHEST','/f')
& schtasks.exe @create | Out-Null
if ($LASTEXITCODE -ne 0) { throw "schtasks /create failed ($LASTEXITCODE)" }

Step 'Starting task'
& schtasks.exe @('/run','/tn',$TaskName) | Out-Null
if ($LASTEXITCODE -ne 0) { throw "schtasks /run failed ($LASTEXITCODE)" }

$deadline = (Get-Date).AddMinutes($TimeoutMin)
while (-not (Test-Path $done)) {
    if ((Get-Date) -gt $deadline) {
        Note "timeout after $TimeoutMin min - task left running, log: $log"
        Get-Content $log -Tail 40 -ErrorAction SilentlyContinue
        exit 2
    }
    Start-Sleep -Seconds 10
    if (Test-Path $log) {
        $tail = Get-Content $log -Tail 1 -ErrorAction SilentlyContinue
        if ($tail) { Note $tail }
    }
}

$code = (Get-Content $done -Raw).Trim()
Step "Task finished, exit code $code - log follows"
Get-Content $log -ErrorAction SilentlyContinue
schtasks /delete /tn $TaskName /f 2>$null | Out-Null
exit [int]$code
