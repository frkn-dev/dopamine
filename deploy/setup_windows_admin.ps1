<#
Dopamine Windows build host - ADMIN-ONLY bootstrap.

Everything that needs an elevated token lives here, because a Windows OpenSSH
session never gets one. Run it either
  * from an elevated PowerShell on the box:  .\setup_windows_admin.ps1
  * or remotely, without UAC, through a SYSTEM scheduled task:
      ssh <user>@<win> "powershell -NoProfile -ExecutionPolicy Bypass -File C:\build\dopamine\deploy\run_windows_admin_task.ps1"

The rest of the setup (Qt, IFW, WiX, git, zlib, repo) needs no elevation and is
done by deploy\setup_windows_build.ps1.

Idempotent: every step checks first and reports "already ..." when done.
#>
[CmdletBinding()]
param(
    [string]$Root    = 'C:\build',
    [string]$QtRoot  = 'C:\Qt',
    [switch]$SkipVs,
    [switch]$SkipOpenSSH,
    [switch]$SkipPerf
)

$ErrorActionPreference = 'Stop'
function Step($m) { Write-Host "`n=== $m ===" -ForegroundColor Cyan }
function Note($m) { Write-Host "    $m" -ForegroundColor DarkGray }

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
        ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { throw 'Run elevated (or via deploy\run_windows_admin_task.ps1).' }

# ---------------------------------------------------------------- performance
if (-not $SkipPerf) {
    Step 'Performance: power plan, sleep off, Defender exclusions'
    try {
        powercfg /setactive SCHEME_MIN | Out-Null
        powercfg /change monitor-timeout-ac 0 | Out-Null
        powercfg /change standby-timeout-ac 0 | Out-Null
        Note 'power plan = High performance, sleep disabled'
    } catch { Note "powercfg failed: $($_.Exception.Message)" }

    # Real-time scanning of compiler output is the single biggest build slowdown
    # on Windows. Failures are non-fatal (Defender may be managed by policy).
    try {
        Add-MpPreference -ExclusionPath $Root, $QtRoot, $env:TEMP, 'C:\ProgramData\chocolatey' -ErrorAction SilentlyContinue
        foreach ($p in 'cl.exe','link.exe','lib.exe','rc.exe','ml64.exe','ninja.exe','cmake.exe',
                       'msbuild.exe','clang-cl.exe','dotnet.exe','windeployqt.exe','binarycreator.exe',
                       'archivegen.exe','cpack.exe','python.exe') {
            Add-MpPreference -ExclusionProcess $p -ErrorAction SilentlyContinue
        }
        Note "Defender exclusions: $Root, $QtRoot, $env:TEMP + compiler processes"
    } catch { Note "Defender exclusions skipped: $($_.Exception.Message)" }
}

# ------------------------------------------------------------------- MSVC/SDK
if (-not $SkipVs) {
    Step 'Visual Studio 2022 Build Tools (MSVC x64 + Windows SDK)'
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $haveVs = (Test-Path $vswhere) -and
              (& $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
    if ($haveVs) {
        Note 'already installed'
    } else {
        # The official bootstrapper instead of winget: winget misbehaves in a
        # SYSTEM context (no user profile / MSIX), and this is the path used when
        # the whole script is driven through the SYSTEM scheduled task.
        $exe = Join-Path $env:TEMP 'vs_BuildTools.exe'
        if (-not (Test-Path $exe)) {
            Note 'downloading vs_BuildTools.exe'
            curl.exe -L --fail -o $exe 'https://aka.ms/vs/17/release/vs_BuildTools.exe'
        }
        Note 'installing VCTools workload (~15 min, silent)'
        $p = Start-Process -FilePath $exe -Wait -PassThru -ArgumentList @(
            '--quiet','--wait','--norestart','--nocache',
            '--add','Microsoft.VisualStudio.Workload.VCTools',
            '--includeRecommended'
        )
        Note "bootstrapper exit code: $($p.ExitCode) (0 = ok, 3010 = reboot needed)"
        if (-not (Test-Path "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat")) {
            throw 'VS Build Tools install did not produce vcvars64.bat - install by hand and rerun with -SkipVs'
        }
    }
}

# ------------------------------------------------------------------- OpenSSH
if (-not $SkipOpenSSH) {
    Step 'OpenSSH Server (remote builds run over ssh from the mac)'
    $sshdCap = Get-WindowsCapability -Online -Name 'OpenSSH.Server*' | Select-Object -First 1
    if ($sshdCap.State -ne 'Installed') {
        Add-WindowsCapability -Online -Name $sshdCap.Name | Out-Null
        Note "installed $($sshdCap.Name)"
    } else { Note 'capability already installed' }

    Set-Service -Name sshd -StartupType Automatic
    if ((Get-Service sshd).Status -ne 'Running') { Start-Service sshd }
    if (-not (Get-NetFirewallRule -Name 'OpenSSH-Server-In-TCP' -ErrorAction SilentlyContinue)) {
        New-NetFirewallRule -Name 'OpenSSH-Server-In-TCP' -DisplayName 'OpenSSH Server (sshd)' -Enabled True `
            -Direction Inbound -Protocol TCP -Action Allow -LocalPort 22 | Out-Null
        Note 'firewall rule added (tcp/22)'
    }
    # Remote build commands are cmd.exe one-liners (build_windows_local.bat), and
    # PowerShell's default shell would eat the backslashes.
    New-ItemProperty -Path 'HKLM:\SOFTWARE\OpenSSH' -Name DefaultShell `
        -Value 'C:\Windows\System32\cmd.exe' -PropertyType String -Force | Out-Null
    Note 'sshd running, default shell = cmd.exe'
}

# --------------------------------------------------------------------- checks
Step 'Admin checks'
$vcvars = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
$ok = Test-Path $vcvars
Write-Host ("  [{0}] vcvars64.bat" -f $(if ($ok) { 'ok' } else { '--' })) -ForegroundColor $(if ($ok) { 'Green' } else { 'Red' })
$ok = (Get-Service sshd -ErrorAction SilentlyContinue).Status -eq 'Running'
Write-Host ("  [{0}] sshd running" -f $(if ($ok) { 'ok' } else { '--' })) -ForegroundColor $(if ($ok) { 'Green' } else { 'Red' })
$ok = (Get-ItemProperty 'HKLM:\SOFTWARE\OpenSSH' -Name DefaultShell -ErrorAction SilentlyContinue).DefaultShell -eq 'C:\Windows\System32\cmd.exe'
Write-Host ("  [{0}] DefaultShell = cmd.exe" -f $(if ($ok) { 'ok' } else { '--' })) -ForegroundColor $(if ($ok) { 'Green' } else { 'Red' })

Write-Host "`nNext: run the non-admin part as the build user:" -ForegroundColor Cyan
Write-Host @"
  cd <repo>\deploy
  .\setup_windows_build.ps1 -Root $Root -QtRoot $QtRoot -SkipVs -SkipAdmin -GitHubKeyPath <key>
"@ -ForegroundColor DarkGray
