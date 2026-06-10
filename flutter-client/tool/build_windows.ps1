# Builds a self-contained Windows desktop bundle (exe + DLLs + assets).
param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
if (-not $env:VCPKG_ROOT) {
    $env:VCPKG_ROOT = "C:\dev\vcpkg"
}
$flutter = "C:\dev\flutter\bin\flutter.bat"
$vsRoot = "C:\dev\vs2022buildtools"
$cmake = Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path $cmake)) {
    $cmake = "C:\dev\android-sdk\cmake\3.22.1\bin\cmake.exe"
}
if (-not (Test-Path $cmake)) {
    throw "cmake.exe not found"
}

function Copy-VcpkgRuntimeDlls {
    param(
        [string]$DistDir,
        [string]$Configuration
    )
    $vcpkgRoot = $env:VCPKG_ROOT
    if (-not $vcpkgRoot) {
        Write-Warning "VCPKG_ROOT not set; skipping opus.dll copy"
        return
    }
    $vcpkgBin = if ($Configuration -eq "Debug") {
        Join-Path $vcpkgRoot "installed\x64-windows\debug\bin"
    } else {
        Join-Path $vcpkgRoot "installed\x64-windows\bin"
    }
    # owalkie_core.dll links opus dynamically; Boost is static in our vcpkg triplet.
    foreach ($dll in @("opus.dll")) {
        $src = Join-Path $vcpkgBin $dll
        if (-not (Test-Path $src)) {
            throw "Missing runtime dependency: $src (run vcpkg install opus --triplet x64-windows)"
        }
        Copy-Item $src (Join-Path $DistDir $dll) -Force
        Write-Host "Bundled runtime: $dll" -ForegroundColor DarkGray
    }
    if ($Configuration -eq "Debug") {
        Write-Warning "Debug build uses MSVC *debug* runtime (MSVCP140D etc.)."
        Write-Warning "For distribution use: -Configuration Release"
    }
}

function Copy-Bundle {
    param(
        [string]$SourceDir,
        [string]$DestDir
    )
    if (Test-Path $DestDir) {
        Remove-Item -Recurse -Force $DestDir
    }
    New-Item -ItemType Directory -Force -Path $DestDir | Out-Null
    # /E = subdirs, /NFL /NDL /NJH /NJS = quiet
    & robocopy $SourceDir $DestDir /E /NFL /NDL /NJH /NJS | Out-Null
    if ($LASTEXITCODE -ge 8) {
        throw "robocopy failed copying bundle to $DestDir (exit $LASTEXITCODE)"
    }
}

Push-Location $ProjectRoot
try {
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "setup_plugin_junctions.ps1")
    & $flutter pub get | Out-Null

    $buildDir = Join-Path $ProjectRoot "build\windows\x64"
    $sourceDir = Join-Path $ProjectRoot "windows"
    $bundleDir = Join-Path $buildDir "runner\$Configuration"
    $distDir = Join-Path $ProjectRoot "build\dist\windows-$($Configuration.ToLower())"

    New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
    & $cmake -S $sourceDir -B $buildDir -G "Visual Studio 17 2022" -A x64
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

    # INSTALL copies flutter_windows.dll, plugin DLLs, icudtl.dat, flutter_assets.
    & $cmake --build $buildDir --config $Configuration --target INSTALL
    if ($LASTEXITCODE -ne 0) { throw "cmake build/install failed" }

    $exe = Join-Path $bundleDir "owalkie_app.exe"
    if (-not (Test-Path $exe)) {
        throw "Expected binary not found: $exe"
    }
    foreach ($required in @("flutter_windows.dll", "owalkie_core.dll")) {
        $path = Join-Path $bundleDir $required
        if (-not (Test-Path $path)) {
            throw "Bundle incomplete: missing $required in $bundleDir"
        }
    }
    $assetsDir = Join-Path $bundleDir "data\flutter_assets"
    if (-not (Test-Path $assetsDir)) {
        throw "Bundle incomplete: missing data\flutter_assets in $bundleDir"
    }

    Copy-Bundle -SourceDir $bundleDir -DestDir $distDir
    Copy-VcpkgRuntimeDlls -DistDir $distDir -Configuration $Configuration
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "stage_runtime.ps1") -ProjectRoot $ProjectRoot

    Write-Host ""
    Write-Host "Bundle (build tree): $bundleDir" -ForegroundColor Cyan
    Write-Host "Dist (run from here): $distDir" -ForegroundColor Green
    Write-Host "  $($distDir)\owalkie_app.exe" -ForegroundColor Green
} finally {
    Pop-Location
}
