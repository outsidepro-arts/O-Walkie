# Builds Boost.Beast + Opus for Android via system vcpkg (C:\dev\vcpkg).
# Requires: git (first run only), Android NDK.

$ErrorActionPreference = "Stop"

$VcpkgRoot = $env:VCPKG_ROOT
if (-not $VcpkgRoot -or -not (Test-Path $VcpkgRoot)) {
    $VcpkgRoot = "C:\dev\vcpkg"
}

$NdkHome = $env:ANDROID_NDK_HOME
if (-not $NdkHome) {
    $NdkHome = "C:\dev\android-sdk\ndk\26.1.10909125"
}
if (-not (Test-Path $NdkHome)) {
    throw "Android NDK not found at $NdkHome. Set ANDROID_NDK_HOME."
}

if (-not (Test-Path (Join-Path $VcpkgRoot "vcpkg.exe"))) {
    if (-not (Test-Path $VcpkgRoot)) {
        New-Item -ItemType Directory -Force -Path (Split-Path $VcpkgRoot -Parent) | Out-Null
        git clone --depth 1 https://github.com/microsoft/vcpkg $VcpkgRoot
    }
    & (Join-Path $VcpkgRoot "bootstrap-vcpkg.bat") -disableMetrics
}

$env:ANDROID_NDK_HOME = $NdkHome
$BinCache = $env:VCPKG_DEFAULT_BINARY_CACHE
if (-not $BinCache) {
    $BinCache = "C:\dev\vcpkg-bincache"
}
New-Item -ItemType Directory -Force -Path $BinCache | Out-Null
$env:VCPKG_DEFAULT_BINARY_CACHE = $BinCache
$Vcpkg = Join-Path $VcpkgRoot "vcpkg.exe"

$Triplets = @(
    @{ Abi = "arm64-v8a"; Triplet = "arm64-android" },
    @{ Abi = "armeabi-v7a"; Triplet = "arm-neon-android" },
    @{ Abi = "x86_64"; Triplet = "x64-android" }
)

foreach ($t in $Triplets) {
    Write-Host "=== vcpkg install boost-beast opus ($($t.Triplet)) ===" -ForegroundColor Cyan
    & $Vcpkg install boost-beast opus --triplet $t.Triplet
    if ($LASTEXITCODE -ne 0) { throw "vcpkg failed for $($t.Triplet)" }
}

Write-Host "Done. vcpkg root: $VcpkgRoot" -ForegroundColor Green
