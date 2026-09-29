param()
$ErrorActionPreference = 'Stop'
$cmake = Get-Content -LiteralPath (Join-Path (Split-Path -Parent $PSScriptRoot) 'CMakeLists.txt') -Raw
$parts = foreach ($part in @('MAJOR', 'MINOR', 'PATCH')) {
    $match = [regex]::Match($cmake, "(?m)^set\(Launcher_VERSION_$part\s+(\d+)\)\s*$")
    if (-not $match.Success) { throw "Missing Launcher_VERSION_$part in CMakeLists.txt" }
    $match.Groups[1].Value
}
$parts -join '.'
