# Dopamine Windows build host bootstrap (run as Administrator).
#
# A fresh Windows install cannot build the client: deploy\build_windows_local.bat
# expects MSVC 2022 Build Tools, Qt 6.10.1 msvc2022_64, Qt Installer Framework 4.7,
# WiX 4 CLI, Ninja, CMake and the zlib prebuilt; CMakeLists.txt:80 sets
# CPACK_WIX_VERSION 4, so WiX 3.x is not enough.
#
# Usage (elevated PowerShell):
#   Set-ExecutionPolicy -Scope Process Bypass -Force
#   .\setup_windows_build.ps1 -CloneUrl git@github.com:frkn-dev/dopamine.git
#   .\setup_windows_build.ps1 -SkipVs -SkipQt        # only redo the fast parts
#
# Optional: -PfxPath C:\certs\codesign.pfx -PfxPassword ***  (imports the
# "Privacy Technologies OU" code-signing cert that build_windows.bat calls
# signtool with; without it the build succeeds but ships unsigned).

[CmdletBinding()]
param(
    [string]$Root       = 'C:\build',
    [string]$QtRoot     = 'C:\Qt',
    [string]$CloneUrl   = '',
    [string]$PfxPath    = '',
    [string]$AuthorizedKey = '',   # mac public key for remote builds, e.g. content of ~/.ssh/dopamine_win.pub
    [string]$GitHubKeyPath = '',   # private key for git@github.com (frkn-dev/3rd-prebuilt submodule is private)
    [string]$MirrorUrl = '',       # optional LAN mirror, e.g. http://192.168.1.11:8000/mirror
    [string]$IfwVersion = '4.7.0',
    [switch]$SkipVs,
    [switch]$SkipQt,
    [switch]$SkipTools,
    [switch]$SkipPerf,
    [switch]$SkipAdmin      # admin-only steps already done (see deploy\setup_windows_admin.ps1)
)

$ErrorActionPreference = 'Stop'
function Step($m) { Write-Host "`n=== $m ===" -ForegroundColor Cyan }
function Note($m) { Write-Host "    $m" -ForegroundColor DarkGray }

# Runs both from an elevated shell and from a plain ssh session (Windows OpenSSH
# never gives you an elevated token). Admin-only parts are skipped when not
# elevated - do them once with deploy\setup_windows_admin.ps1, which can be driven
# remotely through a SYSTEM scheduled task (deploy\run_windows_admin_task.ps1).
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
        ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Note 'NOT elevated: admin-only steps are skipped (VS BuildTools, OpenSSH, power plan, Defender)'
}

$repoDir = Join-Path $Root 'dopamine'

# ------------------------------------------------------------------ downloads
# GitHub releases abort mid-transfer on this network (curl 56 "Connection was
# aborted"), which used to kill the whole setup half-way. Every archive goes
# through Get-File: an optional LAN mirror first, then the official URL, each
# retried, with a size sanity check after every attempt.
function Get-File([string]$Name, [string]$Url, [int]$MinBytes = 10000) {
    $dst = Join-Path $env:TEMP $Name
    if ((Test-Path $dst) -and (Get-Item $dst).Length -ge $MinBytes) {
        Note "cached: $Name ($([math]::Round((Get-Item $dst).Length/1MB,1)) MB)"
        return $dst
    }
    $sources = @()
    if ($MirrorUrl) { $sources += "$MirrorUrl/$Name" }
    $sources += $Url
    foreach ($src in $sources) {
        for ($try = 1; $try -le 3; $try++) {
            Note "downloading $src (attempt $try)"
            curl.exe -L --fail --retry 2 --retry-delay 2 --connect-timeout 20 -s -S -o $dst $src
            if ($LASTEXITCODE -eq 0 -and (Test-Path $dst) -and (Get-Item $dst).Length -ge $MinBytes) {
                Note "  ok: $([math]::Round((Get-Item $dst).Length/1MB,1)) MB"
                return $dst
            }
            Note "  attempt $try failed (exit $LASTEXITCODE), retrying"
            Start-Sleep -Seconds 3
        }
    }
    throw "could not download $Name from any source"
}

function Get-Zip([string]$Name, [string]$Url, [string]$Dest) {
    $zip = Get-File $Name $Url
    Expand-Archive -Path $zip -DestinationPath $Dest -Force
}

# ---------------------------------------------------------------- performance
if (-not $SkipPerf -and -not $SkipAdmin -and $isAdmin) {
    Step 'Performance: power plan, Defender exclusions'

    # Full rebuilds are CPU-bound; Balanced throttles clocks and parks cores.
    powercfg /setactive SCHEME_MIN | Out-Null
    powercfg /change monitor-timeout-ac 0  | Out-Null
    powercfg /change standby-timeout-ac 0  | Out-Null
    Note 'power plan = High performance, sleep disabled'

    # Real-time scanning of cl.exe/link.exe output is the single biggest build
    # slowdown on Windows (easily 20-40% of a full rebuild).
    Add-MpPreference -ExclusionPath $Root, $QtRoot, $env:TEMP, 'C:\ProgramData\chocolatey' `
        -ErrorAction SilentlyContinue
    foreach ($p in 'cl.exe','link.exe','lib.exe','rc.exe','rc.exe','ml64.exe','ninja.exe','cmake.exe',
                   'msbuild.exe','clang-cl.exe','dotnet.exe','windeployqt.exe','binarycreator.exe','archivegen.exe','cpack.exe') {
        Add-MpPreference -ExclusionProcess $p -ErrorAction SilentlyContinue
    }
    Note "Defender exclusions: $Root, $QtRoot, $env:TEMP + compiler processes"
}

# ------------------------------------------------------------------- MSVC/SDK
if (-not $SkipVs -and -not $SkipAdmin) {
    if (-not $isAdmin) {
        Note 'SKIP VS BuildTools (needs elevation - run deploy\setup_windows_admin.ps1)'
    } else {
    Step 'Visual Studio 2022 Build Tools (MSVC x64 + Windows SDK)'
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $haveVs = (Test-Path $vswhere) -and
              (& $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
    if ($haveVs) {
        Note 'already installed'
    } else {
        winget install --id Microsoft.VisualStudio.2022.BuildTools --accept-source-agreements --accept-package-agreements `
            --override '--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended' | Out-Null
        Note 'installed VCTools workload (skip with -SkipVs)'
    }
    }
}

# --------------------------------------------------------------------- tools
# Everything below installs into the build user's profile or C:\tools. winget is
# deliberately NOT used: its default packages are machine-scope installers that
# need elevation, and an ssh session never has one.
$toolsDir = 'C:\tools'
if (-not $SkipTools) {
    Step "Portable toolchain -> $toolsDir (no admin)"
    New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null

    if (-not (Test-Path "$toolsDir\cmake\bin\cmake.exe")) {
        $cmakeVer = '3.31.6'
        Get-Zip "cmake-$cmakeVer-windows-x86_64.zip" "https://github.com/Kitware/CMake/releases/download/v$cmakeVer/cmake-$cmakeVer-windows-x86_64.zip" $toolsDir
        if (Test-Path "$toolsDir\cmake-$cmakeVer-windows-x86_64") {
            Move-Item "$toolsDir\cmake-$cmakeVer-windows-x86_64" "$toolsDir\cmake" -Force
        }
        Note "cmake $cmakeVer"
    } else { Note 'cmake present' }

    if (-not (Test-Path "$toolsDir\ninja\ninja.exe")) {
        New-Item -ItemType Directory -Force -Path "$toolsDir\ninja" | Out-Null
        Get-Zip 'ninja-win.zip' 'https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip' "$toolsDir\ninja"
        Note 'ninja 1.12.1'
    } else { Note 'ninja present' }

    if (-not (Test-Path "$toolsDir\PortableGit\cmd\git.exe")) {
        $gitVer = '2.47.1'
        $sfxName = "PortableGit-$gitVer-64-bit.7z.exe"
        $sfx = Get-File $sfxName "https://github.com/git-for-windows/git/releases/download/v$gitVer.windows.1/$sfxName" 1000000
        & $sfx -o"$toolsDir\PortableGit" -y | Out-Null
        Note 'PortableGit unpacked'
    } else { Note 'git present' }

    # Python: per-user installer (InstallAllUsers=0), no admin. The Windows Store
    # python.exe stub is not a usable interpreter, so check it really runs.
    $pyOk = $false
    $pyCmd = Get-Command python -ErrorAction SilentlyContinue
    if ($pyCmd -and $pyCmd.Source -notlike '*WindowsApps*') {
        & python -c 'import sys' 2>$null
        $pyOk = ($LASTEXITCODE -eq 0)
    }
    if (-not $pyOk) {
        $pyVer = '3.12.8'
        $pyExe = Get-File "python-$pyVer-amd64.exe" "https://www.python.org/ftp/python/$pyVer/python-$pyVer-amd64.exe" 1000000
        Note "installing Python $pyVer per-user"
        Start-Process -FilePath $pyExe -Wait -ArgumentList '/quiet','InstallAllUsers=0','PrependPath=1',
            'Include_test=0','Include_launcher=1','Include_pip=1'
    } else { Note "python present: $($pyCmd.Source)" }

    # .NET SDK into the user profile - dotnet-install.ps1 needs no admin.
    # dot.net redirects and the TLS handshake fails on this network (schannel),
    # so the script is fetched through Get-File like every other download.
    $dotnetExe = "$env:USERPROFILE\.dotnet\dotnet.exe"
    if (-not (Test-Path $dotnetExe)) {
        $di = Get-File 'dotnet-install.ps1' 'https://dot.net/v1/dotnet-install.ps1'
        & powershell -NoProfile -ExecutionPolicy Bypass -File $di -Channel 8.0 -InstallDir "$env:USERPROFILE\.dotnet"
        if (-not (Test-Path $dotnetExe)) { throw "dotnet SDK install failed: $dotnetExe missing" }
        Note 'dotnet SDK 8 -> %USERPROFILE%\.dotnet'
    } else { Note "dotnet present: $dotnetExe" }

    # PATH: prepend the portable dirs for this session and for the build user, so
    # later ssh sessions (and build_windows_local.bat's `where ninja`) find them.
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    foreach ($p in "$toolsDir\cmake\bin", "$toolsDir\ninja", "$toolsDir\PortableGit\cmd", "$env:USERPROFILE\.dotnet", "$env:USERPROFILE\.dotnet\tools") {
        if ($userPath -notlike "*$p*") { $userPath = "$p;$userPath" }
    }
    [Environment]::SetEnvironmentVariable('Path', $userPath, 'User')
    $env:Path = "$toolsDir\cmake\bin;$toolsDir\ninja;$toolsDir\PortableGit\cmd;$env:USERPROFILE\.dotnet;$env:USERPROFILE\.dotnet\tools;" +
                [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + $userPath

    Step 'WiX 4 CLI (CPack -G WIX with CPACK_WIX_VERSION=4)'
    # build_windows.bat looks for wix.exe in %USERPROFILE%\.dotnet\tools - pin
    # both the tool and the Util extension, otherwise -G WIX refuses the patch file.
    # The .NET SDK lives in the profile, not in Program Files, so the apphost needs
    # DOTNET_ROOT or every tool aborts with "Failed to resolve hostfxr.dll".
    [Environment]::SetEnvironmentVariable('DOTNET_ROOT', "$env:USERPROFILE\.dotnet", 'User')
    $env:DOTNET_ROOT = "$env:USERPROFILE\.dotnet"
    & $dotnetExe tool install --global wix --version 4.0.5 2>$null | Out-Null
    $wix = "$env:USERPROFILE\.dotnet\tools\wix.exe"
    if (-not (Test-Path $wix)) { throw "wix.exe not installed at $wix" }
    & $wix extension add -g WixToolset.Util.wixext/4.0.5 2>&1 | Out-Null
    & $wix --version
}

# ------------------------------------------------------------------------ Qt
if (-not $SkipQt) {
    Step "Qt 6.10.1 msvc2022_64 -> $QtRoot"
    # python was installed a moment ago in this same run: the new PATH only exists
    # in the registry, so re-read it and fall back to the per-user install dir.
    $env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' +
                [Environment]::GetEnvironmentVariable('Path','User')
    $py = (Get-Command python -ErrorAction SilentlyContinue).Source
    if (-not $py -or $py -like '*WindowsApps*') {
        $cand = Get-ChildItem "$env:LOCALAPPDATA\Programs\Python\Python3*\python.exe" -ErrorAction SilentlyContinue |
                Sort-Object FullName -Descending | Select-Object -First 1
        if ($cand) { $py = $cand.FullName; $env:Path = "$($cand.DirectoryName);$($cand.DirectoryName)\Scripts;$env:Path" }
    }
    if (-not $py) { throw 'python not found - install it per-user and rerun with -SkipTools' }
    Note "python: $py"

    & $py -m pip install --upgrade aqtinstall | Out-Null
    # -m all: the project pulls Core Gui Network Xml RemoteObjects Quick Svg
    # QuickControls2 Core5Compat Concurrent Widgets (+ DBus in the service);
    # a base-only install fails in find_package().
    # Qt's CDN drops connections on this network too, so retry a few times.
    for ($try = 1; $try -le 4; $try++) {
        & $py -m aqt install-qt windows desktop 6.10.1 win64_msvc2022_64 -m all -O $QtRoot
        if (Test-Path (Join-Path $QtRoot '6.10.1\msvc2022_64\bin\qt-cmake.bat')) { break }
        Note "aqt attempt $try failed, retrying"
        Start-Sleep -Seconds 5
    }
    if (-not (Test-Path (Join-Path $QtRoot '6.10.1\msvc2022_64\bin\qt-cmake.bat'))) {
        throw "Qt install incomplete: expected $QtRoot\6.10.1\msvc2022_64\bin\qt-cmake.bat"
    }

    Step "Qt Installer Framework $IfwVersion -> $QtRoot\Tools\QtInstallerFramework\4.7"
    $ifwDir = Join-Path $QtRoot 'Tools\QtInstallerFramework\4.7'
    if (-not (Test-Path (Join-Path $ifwDir 'bin\binarycreator.exe'))) {
        New-Item -ItemType Directory -Force -Path $ifwDir | Out-Null
        $exe = Join-Path $env:TEMP "QtInstallerFramework-windows-x64-$IfwVersion.exe"
        $url = "https://download.qt.io/official_releases/qt-installer-framework/$IfwVersion/QtInstallerFramework-windows-x64-$IfwVersion.exe"
        $exe = Get-File "QtInstallerFramework-windows-x64-$IfwVersion.exe" $url 1000000
        # IFW installers are silent-capable without a Qt account.
        & $exe --am --al -c --root $ifwDir install | Out-Null
        if (-not (Test-Path (Join-Path $ifwDir 'bin\binarycreator.exe'))) {
            throw "IFW install failed - install it by hand into $ifwDir (needs bin\binarycreator.exe + bin\archivegen.exe)"
        }
    } else { Note 'already installed' }

    # build_windows_local.bat defaults QT_ROOT_DIR to a hard-coded path from the
    # old build box; persist the real location. Machine scope needs elevation, so
    # fall back to the user scope an ssh session can write.
    try {
        [Environment]::SetEnvironmentVariable('QT_ROOT_DIR', $QtRoot, 'Machine')
        Note "QT_ROOT_DIR=$QtRoot (machine-wide)"
    } catch {
        [Environment]::SetEnvironmentVariable('QT_ROOT_DIR', $QtRoot, 'User')
        Note "QT_ROOT_DIR=$QtRoot (user scope - machine scope needs elevation)"
    }
    $env:QT_ROOT_DIR = $QtRoot
}

# ------------------------------------------------------------------- OpenSSH
if (-not $SkipAdmin) {
if (-not $isAdmin) {
    Note 'SKIP OpenSSH server setup (needs elevation - run deploy\setup_windows_admin.ps1)'
} else {
Step 'OpenSSH Server (remote builds run over ssh from the mac)'
$sshdCap = Get-WindowsCapability -Online -Name 'OpenSSH.Server*' | Select-Object -First 1
if ($sshdCap.State -ne 'Installed') {
    Add-WindowsCapability -Online -Name $sshdCap.Name | Out-Null
}
Set-Service -Name sshd -StartupType Automatic
Start-Service sshd
if (-not (Get-NetFirewallRule -Name 'OpenSSH-Server-In-TCP' -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -Name 'OpenSSH-Server-In-TCP' -DisplayName 'OpenSSH Server (sshd)' -Enabled True `
        -Direction Inbound -Protocol TCP -Action Allow -LocalPort 22 | Out-Null
}
# Remote build commands are cmd.exe one-liners (build_windows_local.bat).
New-ItemProperty -Path 'HKLM:\SOFTWARE\OpenSSH' -Name DefaultShell `
    -Value 'C:\Windows\System32\cmd.exe' -PropertyType String -Force | Out-Null
Note 'sshd running, default shell = cmd.exe'
}
}

if ($AuthorizedKey) {
    $me = "$env:USERDOMAIN\$env:USERNAME"
    # Which file sshd reads depends on whether the ACCOUNT is in Administrators,
    # not on whether this shell is elevated (an ssh session never is).
    $acct = "$env:COMPUTERNAME\$env:USERNAME"
    $isMemberOfAdmins = @(Get-LocalGroupMember -Group 'Administrators' -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty Name) -contains $acct
    $keyFile = if ($isMemberOfAdmins) { 'C:\ProgramData\ssh\administrators_authorized_keys' }
               else                  { "$env:USERPROFILE\.ssh\authorized_keys" }
    New-Item -ItemType Directory -Force -Path (Split-Path $keyFile) | Out-Null
    if (-not (Test-Path $keyFile)) { New-Item -ItemType File -Path $keyFile | Out-Null }
    if (-not (Select-String -Path $keyFile -SimpleMatch $AuthorizedKey -Quiet -ErrorAction SilentlyContinue)) {
        Add-Content -Path $keyFile -Value $AuthorizedKey.Trim()
    }
    if ($isMemberOfAdmins) {
        if ($isAdmin) {
            icacls.exe $keyFile /inheritance:r /grant 'Administrators:F' /grant 'SYSTEM:F' | Out-Null
        } else {
            Note 'administrators_authorized_keys written but ACL needs an elevated run'
        }
    }
    if ($isAdmin) { Restart-Service sshd -ErrorAction SilentlyContinue }
    Note "authorized key installed for $me ($keyFile)"
} else {
    Note 'no -AuthorizedKey given: add the mac pubkey by hand, see below'
}

# ------------------------------------------------------------------ git / ssh
Step 'Git config + GitHub access (private submodule)'
# Repo files must keep their LF endings: client/translations is committed with LF
# and BUILD_RELEASE.md's build one-liner has to `git checkout -- client\translations`
# after every build, which only happens if the checkout itself does not rewrite them.
git config --global core.autocrlf false
git config --global core.longpaths true
git config --global core.sshCommand "ssh -o IdentitiesOnly=yes"
git config --global init.defaultBranch main
Note 'core.autocrlf=false, core.longpaths=true'

if ($GitHubKeyPath) {
    # client/3rd-prebuilt (1.4 GB) lives in a private repo, so `git submodule update`
    # fails on a fresh host without a key. Copy the key and pin it in ~/.ssh/config
    # rather than relying on ssh-agent (sshd sessions have no agent).
    $sshDir = Join-Path $env:USERPROFILE '.ssh'
    New-Item -ItemType Directory -Force -Path $sshDir | Out-Null
    $keyDst = Join-Path $sshDir 'id_github'
    Copy-Item $GitHubKeyPath $keyDst -Force
    icacls.exe $keyDst /inheritance:r /grant "$($env:USERNAME):R" | Out-Null
    $cfgPath = Join-Path $sshDir 'config'
    $cfg = @"
Host github.com
    HostName ssh.github.com
    Port 443
    User git
    IdentityFile ~/.ssh/id_github
    IdentitiesOnly yes
"@
    if (-not (Test-Path $cfgPath) -or -not (Select-String -Path $cfgPath -SimpleMatch 'ssh.github.com' -Quiet)) {
        Add-Content -Path $cfgPath -Value $cfg
    }
    if (-not (Test-Path (Join-Path $sshDir 'known_hosts')) -or
        -not (Select-String -Path (Join-Path $sshDir 'known_hosts') -SimpleMatch 'ssh.github.com' -Quiet -ErrorAction SilentlyContinue)) {
        cmd /c "ssh-keyscan -p 443 ssh.github.com 2>nul" | Add-Content -Path (Join-Path $sshDir 'known_hosts')
    }
    Note "github key installed at $keyDst (ssh.github.com:443)"
    cmd /c "ssh -o StrictHostKeyChecking=accept-new -T git@github.com 2>&1" | Select-Object -First 1
} else {
    Note 'no -GitHubKeyPath: `git submodule update` will fail (3rd-prebuilt is private)'
}

# ---------------------------------------------------------------------- repo
if ($CloneUrl) {
    Step "Repo -> $repoDir"
    if (-not (Test-Path $repoDir)) {
        New-Item -ItemType Directory -Force -Path $Root | Out-Null
        git clone $CloneUrl $repoDir
    } else {
        Note 'already cloned, pulling'
        git -C $repoDir pull --ff-only
    }
    # client/3rd-prebuilt is a 1.1 GB private submodule (amnezia_xray, openssl,
    # zlib, xray, awg...) - the build cannot configure without it.
    git -C $repoDir submodule update --init --recursive

    Step 'zlib prebuilt (Windows has no system zlib)'
    if (-not (Test-Path (Join-Path $repoDir 'client\3rd-prebuilt\3rd-prebuilt\zlib\windows\win64\zlibstatic.lib'))) {
        Push-Location $repoDir
        cmd /c 'deploy\build_zlib_windows.bat'
        Pop-Location
    } else { Note 'present' }
}

# ------------------------------------------------------------------- signing
if ($PfxPath) {
    Step 'Code-signing certificate'
    # build_windows.bat runs: signtool sign /n "Privacy Technologies OU"
    # => the cert must sit in CurrentUser\My WITH its private key.
    Import-PfxCertificate -FilePath $PfxPath -CertStoreLocation Cert:\CurrentUser\My | Out-Null
    Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -match 'Privacy Technologies' } |
        Format-List Subject, Thumbprint, NotAfter, HasPrivateKey
} else {
    Note 'no -PfxPath: build will produce UNSIGNED exe/msi (signtool finds no cert)'
}

# --------------------------------------------------------------------- checks
Step 'Result'
# signtool ships with the Windows SDK that the VCTools workload pulls in; it is
# only on PATH inside a vcvars64 shell, which build_windows_local.bat does set up.
$signtool = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe' -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending | Select-Object -First 1
$checks = [ordered]@{
    'MSVC vcvars64.bat'   = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
    'Qt qt-cmake.bat'     = "$QtRoot\6.10.1\msvc2022_64\bin\qt-cmake.bat"
    'Qt windeployqt.exe'  = "$QtRoot\6.10.1\msvc2022_64\bin\windeployqt.exe"
    'IFW binarycreator'   = "$QtRoot\Tools\QtInstallerFramework\4.7\bin\binarycreator.exe"
    'WiX wix.exe'         = "$env:USERPROFILE\.dotnet\tools\wix.exe"
    'signtool.exe'        = $(if ($signtool) { $signtool.FullName } else { 'MISSING' })
    'cmake.exe'           = 'C:\tools\cmake\bin\cmake.exe'
    'ninja.exe'           = 'C:\tools\ninja\ninja.exe'
    'git.exe'             = 'C:\tools\PortableGit\cmd\git.exe'
    'Repo build script'   = "$repoDir\deploy\build_windows_local.bat"
    'zlibstatic.lib'      = "$repoDir\client\3rd-prebuilt\3rd-prebuilt\zlib\windows\win64\zlibstatic.lib"
    '3rd-prebuilt xray'   = "$repoDir\client\3rd-prebuilt\deploy-prebuilt\windows\x64\amnezia_xray.dll"
}
foreach ($k in $checks.Keys) {
    $ok = Test-Path $checks[$k]
    $color = if ($ok) { 'Green' } else { 'Red' }
    Write-Host ("  [{0}] {1}" -f $(if ($ok) { 'ok' } else { '--' }), $k) -ForegroundColor $color
}
$cert = Get-ChildItem Cert:\CurrentUser\My -ErrorAction SilentlyContinue |
        Where-Object { $_.Subject -match 'Privacy Technologies' -and $_.HasPrivateKey }
if ($cert) {
    Write-Host ("  [ok] signing cert {0} valid to {1:yyyy-MM-dd}" -f $cert.Thumbprint, $cert.NotAfter) -ForegroundColor Green
} else {
    Write-Host '  [--] signing cert "Privacy Technologies OU" (build ships UNSIGNED without it)' -ForegroundColor Yellow
}

$ip = (Get-NetIPAddress -AddressFamily IPv4 -PrefixOrigin Dhcp,Manual -ErrorAction SilentlyContinue |
       Where-Object InterfaceAlias -notmatch 'Loopback|vEthernet' | Select-Object -First 1 -ExpandProperty IPAddress)
Write-Host "`nNext (from the mac):" -ForegroundColor Cyan
Write-Host @"
  ssh -i ~/.ssh/dopamine_win $(whoami)@$ip ^
    "cd $repoDir && call deploy\build_windows_local.bat > build.log 2>&1"

  scp -i ~/.ssh/dopamine_win "$(whoami)@${ip}:$($repoDir -replace '\\','/')/deploy/build_64/Dopamine-*-win64.msi" .
"@ -ForegroundColor DarkGray
