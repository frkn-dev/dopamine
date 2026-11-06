<#
Android-тулчейн на вин-боксе: JDK 21, Android SDK (platform-tools, android-35,
build-tools 35, NDK 26.1.10909125) и Qt for Android arm64_v8a для 6.10.1.
Ставится в пользовательские каталоги (C:\tools, C:\Android, C:\Qt) — elevation не нужен.

  ssh winbuild "powershell -NoProfile -ExecutionPolicy Bypass -File C:\setup\setup_android_windows.ps1"

Повторный запуск безопасен: уже скачанное/распакованное пропускается.
#>

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$JDK_DIR    = 'C:\tools\jdk21'
$SDK_ROOT   = 'C:\Android\Sdk'
$QT_ROOT    = 'C:\Qt'
$QT_VERSION = '6.10.1'
$NDK_VER    = '26.1.10909125'
$BUILD_TOOLS = '35.0.0'
$PLATFORM   = 'android-35'

function Say($m) { Write-Output ("[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m) }

function Get-Dir($url, $out) {
    Say "downloading $url"
    Invoke-WebRequest -Uri $url -OutFile $out -UseBasicParsing
    Say ("downloaded {0:N1} MB" -f ((Get-Item $out).Length / 1MB))
}

# --- 1. JDK 21 ---
if (-not (Test-Path (Join-Path $JDK_DIR 'bin\java.exe'))) {
    $zip = 'C:\setup\jdk21.zip'
    if (-not (Test-Path $zip)) {
        Get-Dir 'https://api.adoptium.net/v3/binary/latest/21/ga/windows/x64/jdk/hotspot/normal/eclipse' $zip
    }
    Say 'expanding JDK'
    New-Item -ItemType Directory -Force -Path $JDK_DIR | Out-Null
    Expand-Archive -Path $zip -DestinationPath $JDK_DIR -Force
    # внутри архива обычно один каталог jdk-21.x.y+z
    $inner = Get-ChildItem $JDK_DIR -Directory | Select-Object -First 1
    if ($inner -and -not (Test-Path (Join-Path $JDK_DIR 'bin'))) {
        Get-ChildItem $inner.FullName | Move-Item -Destination $JDK_DIR -Force
        Remove-Item $inner.FullName -Recurse -Force
    }
}
$prevEap = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
$javaOut = (& (Join-Path $JDK_DIR 'bin\java.exe') -version 2>&1 | Out-String)
$ErrorActionPreference = $prevEap
Say ('java: ' + (($javaOut -split "`r?`n") | Where-Object { $_ -match 'version' } | Select-Object -First 1))

# --- 1b. JDK 17 ---
# AGP-проект :plugins требует именно Java 17 toolchain; JAVA_HOME=21 его не
# удовлетворяет, а Gradle ищет toolchain сам — поэтому JDK 17 ставим отдельно.
$JDK17_DIR = 'C:\tools\jdk17'
if (-not (Test-Path (Join-Path $JDK17_DIR 'bin\javac.exe'))) {
    $zip = 'C:\setup\jdk17.zip'
    if (-not (Test-Path $zip)) {
        Get-Dir 'https://api.adoptium.net/v3/binary/latest/17/ga/windows/x64/jdk/hotspot/normal/eclipse' $zip
    }
    Say 'expanding JDK 17'
    New-Item -ItemType Directory -Force -Path $JDK17_DIR | Out-Null
    Expand-Archive -Path $zip -DestinationPath $JDK17_DIR -Force
    $inner = Get-ChildItem $JDK17_DIR -Directory | Select-Object -First 1
    if ($inner -and -not (Test-Path (Join-Path $JDK17_DIR 'bin'))) {
        Get-ChildItem $inner.FullName | Move-Item -Destination $JDK17_DIR -Force
        Remove-Item $inner.FullName -Recurse -Force
    }
}

# --- 2. Android cmdline-tools ---
$sdkManager = Join-Path $SDK_ROOT 'cmdline-tools\latest\bin\sdkmanager.bat'
if (-not (Test-Path $sdkManager)) {
    $zip = 'C:\setup\cmdline-tools.zip'
    if (-not (Test-Path $zip)) {
        Get-Dir 'https://dl.google.com/android/repository/commandlinetools-win-11076708_latest.zip' $zip
    }
    Say 'expanding cmdline-tools'
    $tmp = 'C:\setup\cmdline-tools-tmp'
    Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
    Expand-Archive -Path $zip -DestinationPath $tmp -Force
    New-Item -ItemType Directory -Force -Path (Join-Path $SDK_ROOT 'cmdline-tools') | Out-Null
    if (Test-Path (Join-Path $SDK_ROOT 'cmdline-tools\latest')) {
        Remove-Item (Join-Path $SDK_ROOT 'cmdline-tools\latest') -Recurse -Force
    }
    Move-Item (Join-Path $tmp 'cmdline-tools') (Join-Path $SDK_ROOT 'cmdline-tools\latest')
    Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
}

$env:JAVA_HOME = $JDK_DIR
$env:ANDROID_SDK_ROOT = $SDK_ROOT
$env:ANDROID_HOME = $SDK_ROOT

# --- 3. SDK-пакеты ---
# sdkmanager спрашивает «Accept? (y/N)» на каждую из 8 лицензий — одному echo y
# он не верит и молча ничего не ставит, поэтому кормим файл с запасом строк.
$yesFile = 'C:\setup\yes.txt'
(1..40 | ForEach-Object { 'y' }) | Set-Content -Path $yesFile -Encoding ASCII

Say 'accepting licenses'
& cmd.exe /c "`"$sdkManager`" --sdk_root=`"$SDK_ROOT`" --licenses < `"$yesFile`" > C:\setup\android_licenses.log 2>&1"
Say ('licenses exit: ' + $LASTEXITCODE)

Say "installing platform-tools, $PLATFORM, build-tools $BUILD_TOOLS, NDK $NDK_VER"
& cmd.exe /c "`"$sdkManager`" --sdk_root=`"$SDK_ROOT`" `"platform-tools`" `"platforms;$PLATFORM`" `"build-tools;$BUILD_TOOLS`" `"ndk;$NDK_VER`" < `"$yesFile`" > C:\setup\android_sdk.log 2>&1"
Say ('sdkmanager exit: ' + $LASTEXITCODE)

# --- 4. Qt for Android ---
$qtAndroid = Join-Path $QT_ROOT "$QT_VERSION\android_arm64_v8a"
if (-not (Test-Path (Join-Path $qtAndroid 'bin\qt-cmake.bat'))) {
    Say 'installing aqtinstall'
    & cmd.exe /c "`"C:\Users\Public\python\python.exe`" -m pip --version >nul 2>&1" 2>$null
    $python = (Get-Command python -ErrorAction SilentlyContinue).Source
    if (-not $python) { throw 'python not found' }
    & cmd.exe /c "`"$python`" -m pip install --quiet aqtinstall > C:\setup\aqt_install.log 2>&1"
    Say 'downloading Qt for Android (arm64_v8a)'
    & cmd.exe /c "`"$python`" -m aqt install-qt windows android $QT_VERSION android_arm64_v8a -m qtremoteobjects qt5compat qtshadertools -O `"$QT_ROOT`" >> C:\setup\aqt_install.log 2>&1"
    Say ('aqt exit: ' + $LASTEXITCODE)
}

Say '--- summary ---'
foreach ($p in @(
    (Join-Path $JDK_DIR 'bin\java.exe'),
    (Join-Path $JDK17_DIR 'bin\javac.exe'),
    $sdkManager,
    (Join-Path $SDK_ROOT "platforms\$PLATFORM\android.jar"),
    (Join-Path $SDK_ROOT "build-tools\$BUILD_TOOLS\aapt2.exe"),
    (Join-Path $SDK_ROOT "ndk\$NDK_VER\source.properties"),
    (Join-Path $qtAndroid 'bin\qt-cmake.bat')
)) {
    Say ("{0}: {1}" -f ($(if (Test-Path $p) { 'OK' } else { 'MISSING' })), $p)
}
