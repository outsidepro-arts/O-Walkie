param(
    [string]$Version = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$issPath = Join-Path $PSScriptRoot "owalkie-installer.iss"
$isccPath = "C:\dev\InnoSetup\ISCC.exe"

if (!(Test-Path $isccPath)) {
    throw "Inno Setup compiler not found at $isccPath"
}

$distDir = Join-Path $repoRoot "build\dist"
if (!(Test-Path $distDir)) {
    throw "Desktop dist folder not found: $distDir. Build Windows client first."
}

if ($Version -ne "") {
    $env:OWALKIE_VERSION_STRING = $Version
}

& $isccPath $issPath
