# Copies vcpkg opus.dll next to built Windows runners (flutter run / Debug exe).
param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

if (-not $env:VCPKG_ROOT) {
    $env:VCPKG_ROOT = "C:\dev\vcpkg"
}

$targets = @(
    @{ Config = "Debug"; Bin = Join-Path $env:VCPKG_ROOT "installed\x64-windows\debug\bin\opus.dll" },
    @{ Config = "Release"; Bin = Join-Path $env:VCPKG_ROOT "installed\x64-windows\bin\opus.dll" }
)

foreach ($t in $targets) {
    $dir = Join-Path $ProjectRoot "build\windows\x64\runner\$($t.Config)"
    if (-not (Test-Path $dir)) { continue }
    if (-not (Test-Path $t.Bin)) {
        Write-Warning "Missing $($t.Bin)"
        continue
    }
    Copy-Item $t.Bin (Join-Path $dir "opus.dll") -Force
    Write-Host "Staged opus.dll -> $dir"
}
