# Builds Flutter client for Windows (dist bundle) and Android (debug APK + optional ADB install).
param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [ValidateSet("Debug", "Release")]
    [string]$WindowsConfiguration = "Release",
    [switch]$SkipWindows,
    [switch]$SkipAndroid,
    [switch]$NoInstall,
    [switch]$PrepareAndroidNdk
)

$ErrorActionPreference = "Stop"

function Initialize-BuildEnvironment {
    if (-not $env:Path.Contains("C:\dev\flutter\bin")) {
        $env:Path = "C:\dev\flutter\bin;C:\dev\jdk\bin;" + $env:Path
    }
    if (-not $env:ANDROID_HOME) {
        $env:ANDROID_HOME = "C:\dev\android-sdk"
    }
    if (-not $env:VCPKG_ROOT) {
        $env:VCPKG_ROOT = "C:\dev\vcpkg"
    }
}

function Get-AdbPath {
    $candidates = @(
        (Join-Path $env:ANDROID_HOME "platform-tools\adb.exe"),
        "C:\dev\android-sdk\platform-tools\adb.exe"
    )
    foreach ($path in $candidates) {
        if (Test-Path $path) {
            return $path
        }
    }
    throw "adb.exe not found. Set ANDROID_HOME or install Android platform-tools."
}

function Get-FirstAdbDevice {
    param([string]$Adb)
    $lines = @(& $Adb devices 2>&1 | Where-Object { $_ -match "\tdevice$" })
    if ($lines.Count -eq 0) {
        return $null
    }
    if ($lines.Count -gt 1) {
        Write-Warning "Multiple ADB devices; installing on the first one."
    }
    return ($lines[0] -split "\t")[0]
}

function Install-DebugApk {
    param(
        [string]$ApkPath,
        [string]$Adb
    )
    if (-not (Test-Path $ApkPath)) {
        throw "APK not found: $ApkPath"
    }
    $serial = Get-FirstAdbDevice -Adb $Adb
    if (-not $serial) {
        Write-Warning "No ADB device in 'device' state. Skipping install."
        Write-Warning "APK built at: $ApkPath"
        return
    }
    Write-Host "Installing on $serial ..." -ForegroundColor Cyan
    & $Adb -s $serial install -r $ApkPath
    if ($LASTEXITCODE -ne 0) {
        throw "adb install failed (exit $LASTEXITCODE)"
    }
    Write-Host "Installed ru.outsidepro_arts.owalkie.flutter (debug)" -ForegroundColor Green
}

Initialize-BuildEnvironment
Push-Location $ProjectRoot
try {
    Write-Host "=== O-Walkie Flutter build ===" -ForegroundColor Cyan
    Write-Host "Project: $ProjectRoot"

    flutter pub get
    if ($LASTEXITCODE -ne 0) { throw "flutter pub get failed" }

    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "setup_plugin_junctions.ps1") -ProjectRoot $ProjectRoot

    if ($PrepareAndroidNdk) {
        $ndkScript = Join-Path (Split-Path $ProjectRoot -Parent) "android\scripts\build-ndk-deps.ps1"
        if (-not (Test-Path $ndkScript)) {
            throw "NDK deps script not found: $ndkScript"
        }
        Write-Host "=== Android NDK deps (vcpkg) ===" -ForegroundColor Cyan
        & powershell -ExecutionPolicy Bypass -File $ndkScript
    }

    if (-not $SkipWindows) {
        Write-Host "=== Windows ($WindowsConfiguration) ===" -ForegroundColor Cyan
        & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build_windows.ps1") `
            -ProjectRoot $ProjectRoot `
            -Configuration $WindowsConfiguration
    }

    if (-not $SkipAndroid) {
        Write-Host "=== Android (debug APK) ===" -ForegroundColor Cyan
        $env:OWALKIE_FLUTTER_FULL_SESSION = "ON"
        if (-not $PrepareAndroidNdk) {
            $boostProbe = Join-Path $env:VCPKG_ROOT "installed\arm64-android\share\boost_headers\boost_headers-config.cmake"
            if (-not (Test-Path $boostProbe)) {
                Write-Warning "Android NDK vcpkg deps missing; running build-ndk-deps.ps1 (first time may take a while)."
                $ndkScript = Join-Path (Split-Path $ProjectRoot -Parent) "android\scripts\build-ndk-deps.ps1"
                if (-not (Test-Path $ndkScript)) {
                    throw "NDK deps script not found: $ndkScript"
                }
                & powershell -ExecutionPolicy Bypass -File $ndkScript
            }
        }
        foreach ($cxx in @(
            (Join-Path $ProjectRoot "android\.cxx"),
            (Join-Path $ProjectRoot "android\app\.cxx"),
            (Join-Path $ProjectRoot "packages\owalkie_core\android\.cxx")
        )) {
            if (Test-Path $cxx) {
                Remove-Item -Recurse -Force $cxx
            }
        }
        flutter build apk --debug
        if ($LASTEXITCODE -ne 0) { throw "flutter build apk --debug failed" }

        $apk = Join-Path $ProjectRoot "build\app\outputs\flutter-apk\app-debug.apk"
        Write-Host "APK: $apk" -ForegroundColor Green

        if (-not $NoInstall) {
            $adb = Get-AdbPath
            Install-DebugApk -ApkPath $apk -Adb $adb
        }
    }

    Write-Host ""
    Write-Host "=== Done ===" -ForegroundColor Green
    if (-not $SkipWindows) {
        $dist = Join-Path $ProjectRoot "build\dist\windows-$($WindowsConfiguration.ToLower())"
        Write-Host "Windows dist: $dist\owalkie_app.exe"
    }
} finally {
    Pop-Location
}
