<#
Сборка Android APK на вин-боксе (аналог deploy/build_android.sh для macOS).

  powershell -NoProfile -ExecutionPolicy Bypass -File deploy\build_android_windows.ps1
  powershell ... -File deploy\build_android_windows.ps1 -Abi arm64-v8a -BuildType release

Требует тулчейн из deploy\setup_android_windows.ps1:
  C:\tools\jdk21, C:\Android\Sdk (platform-tools/android-35/build-tools 35/NDK),
  C:\Qt\6.10.1\msvc2022_64 (host) и C:\Qt\6.10.1\android_arm64_v8a.

Подпись release берётся из env: ANDROID_KEYSTORE_PATH, ANDROID_KEYSTORE_KEY_ALIAS,
ANDROID_KEYSTORE_KEY_PASS (как в client\android\build.gradle.kts).
#>

[CmdletBinding()]
param(
    [string]$Abi = 'arm64-v8a',
    [ValidateSet('release', 'debug')][string]$BuildType = 'release',
    [string]$JdkDir = 'C:\tools\jdk21',
    [string]$SdkRoot = 'C:\Android\Sdk',
    [string]$QtRoot = 'C:\Qt',
    [string]$QtVersion = '6.10.1',
    [string]$NdkVersion = '26.1.10909125'
)

$ErrorActionPreference = 'Stop'
function Say($m) { Write-Output ("[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m) }

$repo = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repo 'deploy\build'
$qtAndroid = Join-Path $QtRoot "$QtVersion\android_arm64_v8a"
$qtHost = Join-Path $QtRoot "$QtVersion\msvc2022_64"
$ndkDir = Join-Path $SdkRoot "ndk\$NdkVersion"

foreach ($p in @(
    (Join-Path $JdkDir 'bin\java.exe'),
    (Join-Path $qtAndroid 'bin\qt-cmake.bat'),
    (Join-Path $qtHost 'bin\androiddeployqt.exe'),
    (Join-Path $SdkRoot 'cmdline-tools\latest\bin\sdkmanager.bat'),
    $ndkDir
)) {
    if (-not (Test-Path $p)) { throw "missing toolchain component: $p" }
}

$env:JAVA_HOME = $JdkDir
$env:ANDROID_SDK_ROOT = $SdkRoot
$env:ANDROID_HOME = $SdkRoot
$env:ANDROID_NDK_ROOT = $ndkDir
$env:QT_HOST_PATH = $qtHost
$env:PATH = (Join-Path $JdkDir 'bin') + ';' + $env:PATH

# AGP-плагин :plugins требует Java 17 toolchain — подсказываем Gradle, где он лежит.
$jdk17 = 'C:\tools\jdk17'
if (Test-Path (Join-Path $jdk17 'bin\javac.exe')) {
    $env:GRADLE_OPTS = "-Dorg.gradle.java.installations.paths=$jdk17"
    Say "gradle toolchain JDK: $jdk17"
} else {
    Say 'WARNING: C:\tools\jdk17 not found — gradle may fail on :plugins:compileJava'
}

Say "Qt android: $qtAndroid"
Say "Qt host:    $qtHost"
Say "SDK:        $SdkRoot"
Say "NDK:        $ndkDir"

# 1) configure
Say 'configuring (qt-cmake)'
& cmd.exe /c "`"$(Join-Path $qtAndroid 'bin\qt-cmake.bat')`" -S `"$repo`" -B `"$buildDir`" -DQT_NO_GLOBAL_APK_TARGET_PART_OF_ALL=ON -DCMAKE_BUILD_TYPE=$BuildType -DQT_ANDROID_ABIS=$Abi -G Ninja"
if ($LASTEXITCODE -ne 0) { throw "qt-cmake failed: $LASTEXITCODE" }

# 2) build
Say 'building'
& cmd.exe /c "cmake --build `"$buildDir`" --config $BuildType"
if ($LASTEXITCODE -ne 0) { throw "build failed: $LASTEXITCODE" }

# 3) androiddeployqt (собирает android-build, но gradle запустим сами)
$settings = Get-ChildItem -Path $buildDir -Filter 'android-*-deployment-settings.json' -Recurse |
    Select-Object -First 1
if (-not $settings) { throw 'android deployment settings json not found' }
$outAppDir = Split-Path -Parent $settings.FullName
Say "deploying to $outAppDir"

# androiddeployqt запускает gradle сам, а AGP-проекту :plugins нужен именно
# Java 17 toolchain — переключаем JAVA_HOME на него перед упаковкой.
if (Test-Path (Join-Path $jdk17 'bin\javac.exe')) {
    $env:JAVA_HOME = $jdk17
    $env:PATH = (Join-Path $jdk17 'bin') + ';' + ($env:PATH -replace [regex]::Escape((Join-Path $JdkDir 'bin') + ';'), '')
    Say "switched JAVA_HOME to $jdk17 for gradle"
}

$env:ANDROIDDEPLOYQT_RUN = '1'
$deployArgs = "--input `"$($settings.FullName)`" --output `"$(Join-Path $outAppDir 'android-build')`""
if ($BuildType -eq 'release') { $deployArgs += ' --release' }
& cmd.exe /c "`"$(Join-Path $qtHost 'bin\androiddeployqt.exe')`" $deployArgs"
if ($LASTEXITCODE -ne 0) { throw "androiddeployqt failed: $LASTEXITCODE" }

# 4) gradle
$gradleTask = if ($BuildType -eq 'release') { 'assembleRelease' } else { 'assembleDebug' }
Say "gradle $gradleTask"
$androidBuild = Join-Path $outAppDir 'android-build'
& cmd.exe /c "cd /d `"$androidBuild`" && gradlew.bat --project-dir `"$androidBuild`" -DexplicitRun=1 $gradleTask"
if ($LASTEXITCODE -ne 0) { throw "gradle failed: $LASTEXITCODE" }

$apk = Get-ChildItem -Path (Join-Path $androidBuild "build\outputs\apk\$BuildType") -Filter '*release.apk' -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $apk) {
    $apk = Get-ChildItem -Path (Join-Path $androidBuild 'build\outputs\apk') -Filter '*.apk' -Recurse |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
}
Say ('APK: ' + $apk.FullName + '  ' + $apk.Length + ' bytes')
