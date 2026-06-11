# Prints OWALKIE_VERSION_NAME and OWALKIE_VERSION_CODE from git tags (v*).
# Usage: . .\tool\version_from_git.ps1  (sets env vars in current session)
param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
)

function Get-GitDescribe {
    param([string]$Root)
    try {
        $text = (& git -C $Root describe --tags --match "v*" --long --dirty 2>$null).Trim()
        if ($LASTEXITCODE -eq 0 -and $text) { return $text }
    } catch { }
    return ""
}

function Strip-LeadingV([string]$s) {
    if ($s.StartsWith("v")) { return $s.Substring(1) }
    return $s
}

$describe = Get-GitDescribe -Root $RepoRoot
$versionName = if ($env:OWALKIE_VERSION_NAME) {
    $env:OWALKIE_VERSION_NAME.Trim()
} elseif ($describe) {
    Strip-LeadingV $describe
} else {
    "0.0.0-dev"
}

$versionCode = 1
if ($env:OWALKIE_VERSION_CODE) {
    $versionCode = [int]$env:OWALKIE_VERSION_CODE
} elseif ($describe -match '^v?(\d+)\.(\d+)\.(\d+)-(\d+)-g[0-9a-fA-F]+(-dirty)?$') {
    $major = [int]$Matches[1]
    $minor = [int]$Matches[2]
    $patch = [int]$Matches[3]
    $offset = [Math]::Min([int]$Matches[4], 999)
    $versionCode = $major * 10000000 + $minor * 100000 + $patch * 1000 + $offset
    $versionCode = [Math]::Max(1, [Math]::Min($versionCode, 2100000000))
}

$floorFile = Join-Path $RepoRoot "android\owalkie-version.properties"
if ((Test-Path $floorFile) -and -not $env:OWALKIE_VERSION_CODE) {
    foreach ($line in Get-Content $floorFile) {
        if ($line -match '^versionCodeFloor=(\d+)') {
            $floor = [int]$Matches[1]
            if ($floor -gt $versionCode) { $versionCode = $floor }
        }
    }
}

$env:OWALKIE_VERSION_NAME = $versionName
$env:OWALKIE_VERSION_CODE = "$versionCode"

Write-Host "OWALKIE_VERSION_NAME=$versionName"
Write-Host "OWALKIE_VERSION_CODE=$versionCode"
