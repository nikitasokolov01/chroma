param([string]$Installer, [string]$Version, [string]$PreviousInstaller)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $Version) { $Version = & (Join-Path $PSScriptRoot 'get-chroma-version.ps1') }
if (-not $Installer) { $Installer = Join-Path $projectRoot "dist\release\v$Version\Chroma-$Version-Windows-x64-Setup.exe" }
$fixture = Join-Path $projectRoot ('.chroma-test\installer-' + [guid]::NewGuid().ToString('N'))
$install = Join-Path $fixture 'application'
$prism = Join-Path $fixture 'Prism profile\instances\example\minecraft\saves\world'
New-Item -ItemType Directory -Force -Path $fixture, $prism | Out-Null
$sentinel = Join-Path $prism 'level.dat'
Set-Content -LiteralPath $sentinel -Value 'Synthetic shared Prism world; must be preserved.' -Encoding UTF8
$sentinelHash = (Get-FileHash -LiteralPath $sentinel).Hash
$registryBefore = Test-Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Chroma'
function Invoke-Installer([string]$Path) {
    $process = Start-Process -FilePath $Path -ArgumentList @('/S', '/NOINTEGRATION', "/D=$install") -PassThru -WindowStyle Hidden
    if (-not $process.WaitForExit(60000)) { $process.Kill(); throw 'Owned test installer timed out.' }
    if ($process.ExitCode -ne 0) { throw "Installer failed: $($process.ExitCode)" }
}
Invoke-Installer $(if ($PreviousInstaller) { $PreviousInstaller } else { $Installer })
foreach ($name in @('chroma.exe', 'Uninstall.exe', 'chroma-no-integration', 'LICENSE', 'PRIVACY.md', 'vcruntime140.dll', 'platforms\qwindows.dll', 'iconengines\qsvgicon.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $install $name))) { throw "Missing installed file: $name" }
}
$initialVersion = (Get-Item -LiteralPath (Join-Path $install 'chroma.exe')).VersionInfo.FileVersion
if (Test-Path -LiteralPath (Join-Path $install 'portable.txt')) { throw 'Installer incorrectly enables portable mode.' }
$world = Join-Path $install 'instances\fixture\minecraft\saves\world'
New-Item -ItemType Directory -Force -Path $world | Out-Null
$unknown = Join-Path $world 'level.dat'
Set-Content -LiteralPath $unknown -Value 'Synthetic data added after installation.' -Encoding UTF8
$profile = Join-Path $install 'profile.json'
Set-Content -LiteralPath $profile -Value '{"fixture":"preserve"}' -Encoding UTF8
$unknownHash = (Get-FileHash -LiteralPath $unknown).Hash
$profileHash = (Get-FileHash -LiteralPath $profile).Hash
Invoke-Installer $Installer
if ((Get-Item -LiteralPath (Join-Path $install 'chroma.exe')).VersionInfo.FileVersion -ne "$Version.0") { throw 'Installed application version does not match release.' }
if (-not (Test-Path -LiteralPath (Join-Path $install 'Qt6OpenGLWidgets.dll'))) { throw 'Installed Skin Studio OpenGL runtime is missing.' }
if ((Get-FileHash -LiteralPath $unknown).Hash -ne $unknownHash -or (Get-FileHash -LiteralPath $profile).Hash -ne $profileHash) { throw 'Upgrade modified profile data.' }
$previousPath = $env:PATH
$qtEnvironment = @{}
foreach ($name in @('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_QPA_PLATFORM')) {
    $qtEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    [Environment]::SetEnvironmentVariable($name, $null, 'Process')
}
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $help = Start-Process -FilePath (Join-Path $install 'chroma.exe') -ArgumentList '--help' -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $fixture 'help.txt') -RedirectStandardError (Join-Path $fixture 'help-error.txt')
    if (-not $help.WaitForExit(15000)) { $help.Kill(); throw 'Owned installed --help process timed out.' }
    if ($help.ExitCode -ne 0) { throw "Clean-PATH help failed: $($help.ExitCode)" }
} finally {
    $env:PATH = $previousPath
    foreach ($name in $qtEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $qtEnvironment[$name], 'Process') }
}
$installedHash = (Get-FileHash -LiteralPath (Join-Path $install 'chroma.exe')).Hash
$buildHash = (Get-FileHash -LiteralPath (Join-Path $projectRoot '.tools\build\chroma.exe')).Hash
if ($installedHash -ne $buildHash) { throw 'Installed executable differs from built executable.' }
$uninstall = Start-Process -FilePath (Join-Path $install 'Uninstall.exe') -ArgumentList '/S' -PassThru -WindowStyle Hidden
if (-not $uninstall.WaitForExit(60000)) { $uninstall.Kill(); throw 'Owned test uninstaller timed out.' }
$deadline = [DateTime]::UtcNow.AddSeconds(30)
while ((Test-Path -LiteralPath (Join-Path $install 'chroma.exe')) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 100 }
if (Test-Path -LiteralPath (Join-Path $install 'chroma.exe')) { throw 'Uninstall left installed executable.' }
foreach ($pair in @(@($sentinel, $sentinelHash), @($unknown, $unknownHash), @($profile, $profileHash))) {
    if (-not (Test-Path -LiteralPath $pair[0]) -or (Get-FileHash -LiteralPath $pair[0]).Hash -ne $pair[1]) { throw "Uninstall modified synthetic profile: $($pair[0])" }
}
if ((Test-Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Chroma') -ne $registryBefore) { throw '/NOINTEGRATION changed uninstall registration.' }
[ordered]@{ install = 'passed'; upgrade = 'passed'; fromVersion = $initialVersion; toVersion = "$Version.0"; uninstall = 'passed'; profilePreservation = 'passed'; cleanPathHelp = 'passed'; executableSha256 = $buildHash.ToLowerInvariant(); fixture = $fixture } | ConvertTo-Json
