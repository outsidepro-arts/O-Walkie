# Windows: Flutter plugins expect symlinks; junctions work without Developer Mode.
param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$depsPath = Join-Path $ProjectRoot ".flutter-plugins-dependencies"
if (-not (Test-Path $depsPath)) {
    Write-Error "Run 'flutter pub get' first."
    exit 1
}

$deps = Get-Content $depsPath -Raw | ConvertFrom-Json

function Ensure-Junction {
    param(
        [Parameter(Mandatory)][string]$Link,
        [Parameter(Mandatory)][string]$Target
    )
    if (Test-Path $Link) {
        return
    }
    $parent = Split-Path $Link -Parent
    if (-not (Test-Path $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    $targetResolved = (Resolve-Path $Target).Path
    cmd /c "mklink /J `"$Link`" `"$targetResolved`"" | Out-Null
    if (-not (Test-Path $Link)) {
        throw "Failed to create junction: $Link -> $targetResolved"
    }
}

$platforms = @(
    @{ Name = "windows"; SymlinkDir = Join-Path $ProjectRoot "windows\flutter\ephemeral\.plugin_symlinks" }
    @{ Name = "linux"; SymlinkDir = Join-Path $ProjectRoot "linux\flutter\ephemeral\.plugin_symlinks" }
)

foreach ($platform in $platforms) {
    $pluginList = $deps.plugins.$($platform.Name)
    if ($null -eq $pluginList) { continue }
    foreach ($plugin in $pluginList) {
        $name = $plugin.name
        $path = $plugin.path -replace '\\\\', '\'
        Ensure-Junction -Link (Join-Path $platform.SymlinkDir $name) -Target $path
    }
}

Write-Host "Plugin junctions ready under ephemeral/.plugin_symlinks"
