param(
    [string]$OutputDirectory,
    [ValidateSet('Portable', 'Installed')][string]$PackageKind = 'Portable',
    [string]$RuntimeDirectory,
    [string]$QtRoot,
    [string]$VcpkgRoot,
    [string]$JavaHome,
    [string]$VcVars
)
$ErrorActionPreference = 'Stop'
$releaseVersion = & (Join-Path $PSScriptRoot 'get-chroma-version.ps1')
$environmentArguments = @{ Action = 'Environment' }
foreach ($name in @('QtRoot', 'VcpkgRoot', 'JavaHome', 'VcVars')) {
    if ($PSBoundParameters.ContainsKey($name)) { $environmentArguments[$name] = $PSBoundParameters[$name] }
}
. (Join-Path $PSScriptRoot 'build-chroma.ps1') @environmentArguments
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot ('dist\Chroma-package-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$packageRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
if ((Test-Path -LiteralPath $packageRoot) -and (Get-ChildItem -LiteralPath $packageRoot -Force | Select-Object -First 1)) {
    throw "Packaging requires an empty directory to keep user profiles out of release archives: $packageRoot"
}
if (-not $RuntimeDirectory) { $RuntimeDirectory = Join-Path $toolRoot 'release-runtime' }
$launcher = Join-Path $buildRoot 'chroma.exe'
$deployTool = Join-Path $QtRoot 'bin\windeployqt.exe'
foreach ($required in @($launcher, $deployTool, (Join-Path $RuntimeDirectory 'msvcp140.dll'), (Join-Path $RuntimeDirectory 'vcruntime140.dll'))) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing build artifact or deployment tool: $required" }
}
$builtVersion = (Get-Item -LiteralPath $launcher).VersionInfo.FileVersion
if ($builtVersion -ne "$releaseVersion.0") {
    throw "Built executable version $builtVersion differs from $releaseVersion.0. Rebuild before packaging."
}
New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
& cmake --install $buildRoot --component Runtime --prefix $packageRoot
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Get-ChildItem -LiteralPath $buildRoot -Filter '*.dll' -File | Copy-Item -Destination $packageRoot -Force
$jarsDestination = Join-Path $packageRoot 'jars'
New-Item -ItemType Directory -Force -Path $jarsDestination | Out-Null
Get-ChildItem -LiteralPath (Join-Path $buildRoot 'jars') -Filter '*.jar' -File | Copy-Item -Destination $jarsDestination -Force
if ($PackageKind -eq 'Portable') { Copy-Item -LiteralPath (Join-Path $projectRoot 'program_info\portable.txt') -Destination $packageRoot -Force }
Copy-Item -LiteralPath (Join-Path $projectRoot 'launcher\qtlogging.ini') -Destination $packageRoot -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE'), (Join-Path $projectRoot 'COPYING.md') -Destination $packageRoot -Force
# These deployment options work with the local Qt 6.5 SDK. Upstream's bundle
# component also uses options introduced in newer Qt releases.
& $deployTool --release --no-compiler-runtime --no-translations --no-opengl-sw --no-quick-import --no-system-d3d-compiler --skip-plugin-types generic,networkinformation (Join-Path $packageRoot 'chroma.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if (-not (Test-Path -LiteralPath (Join-Path $packageRoot 'Qt6OpenGLWidgets.dll'))) {
    throw 'Qt deployment did not include the Skin Studio OpenGL widget runtime.'
}
Get-ChildItem -LiteralPath $RuntimeDirectory -Filter '*.dll' -File | Copy-Item -Destination $packageRoot -Force
Set-Content -LiteralPath (Join-Path $packageRoot 'qt.conf') -Encoding ASCII -Value @('[Paths]', 'Plugins=.', 'Libraries=.', 'Prefix=.')
$licenseDestination = Join-Path $packageRoot 'licenses'
New-Item -ItemType Directory -Force -Path $licenseDestination | Out-Null
Get-ChildItem -LiteralPath (Join-Path $installed 'x64-windows\share') -Directory | ForEach-Object {
    $copyright = Join-Path $_.FullName 'copyright'
    if (Test-Path -LiteralPath $copyright) { Copy-Item -LiteralPath $copyright -Destination (Join-Path $licenseDestination ($_.Name + '.txt')) -Force }
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\licenses') -Destination (Join-Path $licenseDestination 'release') -Recurse -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md'), (Join-Path $projectRoot 'docs\RELEASE-LICENSING.md'), (Join-Path $projectRoot 'PRIVACY.md') -Destination $packageRoot -Force
$profileHome = if ($PackageKind -eq 'Portable') { 'this portable folder' } else { '%APPDATA%\Chroma' }
$packageReadme = @"
# Chroma $releaseVersion preview ($PackageKind)

Chroma is an independent native UI fork of Prism Launcher 10.0.5. It is not
an official Prism Launcher or Modrinth release and is not affiliated with or
endorsed by either project. Original contributor credits and licensing remain.

Run chroma.exe and keep the entire folder together. Choose Launcher menu >
Use Prism folder... and select the folder containing prismlauncher.cfg.
Chroma restarts once and remembers the folder. Instances, worlds, accounts,
and launch settings are shared directly. Close Prism while using this profile.
Chroma saves its appearance in chroma-ui.cfg beside the shared configuration,
and remembers the folder in profile.json in $profileHome.
An explicit --dir overrides the remembered folder for that launch.
Microsoft Visual C++ runtime DLLs are included beside the executable.
Their use is subject to the Microsoft runtime terms in
licenses/release/Microsoft-Visual-Cpp-Runtime-14.44.rtf. These terms apply only
to Microsoft's runtime DLLs; Chroma and other open-source components retain
their own licenses. The installer presents these Microsoft terms separately.

This preview retains Prism's public Microsoft OAuth client ID for sign-in.
CurseForge and Imgur credentials are not included. Upstream automatic binary
updates are disabled. Java and Minecraft are not bundled.

Source and build instructions: https://github.com/nikitasokolov01/chroma
Matching application, dependency, and Qt source archives accompany this release:
https://github.com/nikitasokolov01/chroma/releases/tag/v$releaseVersion
Upstream source: https://github.com/PrismLauncher/PrismLauncher
See LICENSE, COPYING.md, THIRD_PARTY_NOTICES.md, RELEASE-LICENSING.md, and licenses/.
Privacy: PRIVACY.md or https://github.com/nikitasokolov01/chroma/blob/main/PRIVACY.md
Uninstalling Chroma preserves launcher profiles, instances, and shared Prism data.
"@
Set-Content -LiteralPath (Join-Path $packageRoot 'README.txt') -Encoding UTF8 -Value $packageReadme
Write-Output "$PackageKind native package: $packageRoot\chroma.exe"
