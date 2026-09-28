param([string]$DataDirectory)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$toolRoot = Join-Path $projectRoot '.tools'
$executable = Join-Path $toolRoot 'build\chroma.exe'
if (-not (Test-Path -LiteralPath $executable)) { throw 'Build Chroma first with scripts/build-chroma.ps1.' }
$qtRoot = Join-Path $toolRoot 'Qt\6.5.3\msvc2019_64'
$env:PATH = "$qtRoot\bin;$toolRoot\vcpkg-installed\x64-windows\bin;$env:PATH"
$env:QT_PLUGIN_PATH = Join-Path $qtRoot 'plugins'
# Normal launches follow the remembered Prism folder. An explicit directory
# remains available for isolated development profiles and recovery.
if ($DataDirectory) {
    $DataDirectory = [System.IO.Path]::GetFullPath($DataDirectory)
    & $executable --dir $DataDirectory
} else {
    & $executable
}
