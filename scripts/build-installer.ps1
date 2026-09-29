param(
    [ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [string]$OutputDirectory,
    [string]$NsisCompiler,
    [string]$RuntimeDirectory
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$sourceVersion = & (Join-Path $PSScriptRoot 'get-chroma-version.ps1')
if (-not $Version) { $Version = $sourceVersion }
if ($Version -ne $sourceVersion) { throw "Installer version $Version differs from application version $sourceVersion." }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot "dist\release\v$Version" }
if (-not $NsisCompiler) { $NsisCompiler = Join-Path $projectRoot '.tools\nsis-3.12-portable\nsis-3.12\makensis.exe' }
if (-not (Test-Path -LiteralPath $NsisCompiler)) { throw 'Run scripts/prepare-release-tools.ps1 or provide -NsisCompiler.' }
$releaseRoot = [IO.Path]::GetFullPath($OutputDirectory)
foreach ($name in @("Chroma-$Version-Windows-x64-Setup.exe", "Chroma-$Version-Windows-x64.zip", 'package-manifest.json')) {
    if (Test-Path -LiteralPath (Join-Path $releaseRoot $name)) { throw "Release output already exists: $name. Choose a new output directory." }
}
New-Item -ItemType Directory -Force -Path $releaseRoot | Out-Null
$stagingRoot = Join-Path $projectRoot ('.tools\release-staging\' + [guid]::NewGuid().ToString('N'))
$installedStage = Join-Path $stagingRoot 'installed'
$portableStage = Join-Path $stagingRoot 'Chroma'
New-Item -ItemType Directory -Force -Path $stagingRoot | Out-Null
$packageArguments = @{ OutputDirectory = $installedStage; PackageKind = 'Installed' }
if ($RuntimeDirectory) { $packageArguments.RuntimeDirectory = $RuntimeDirectory }
& (Join-Path $PSScriptRoot 'package-chroma.ps1') @packageArguments
if ($LASTEXITCODE -ne 0) { throw 'Native package staging failed.' }
if (Test-Path -LiteralPath (Join-Path $installedStage 'portable.txt')) { throw 'Installed package must not be portable.' }

# Keep the zip and installer independent of any folder that has run the app.
Copy-Item -LiteralPath $installedStage -Destination $portableStage -Recurse
Copy-Item -LiteralPath (Join-Path $projectRoot 'program_info\portable.txt') -Destination $portableStage
$portableReadme = Join-Path $portableStage 'README.txt'
$readme = [IO.File]::ReadAllText($portableReadme).Replace('(Installed)', '(Portable)').Replace('%APPDATA%\Chroma', 'this portable folder')
[IO.File]::WriteAllText($portableReadme, $readme, [Text.UTF8Encoding]::new($false))

function ConvertTo-NsisLiteral([string]$Value) { $Value.Replace('$', '$$').Replace('"', '$\"') }
$files = @(Get-ChildItem -LiteralPath $installedStage -File -Recurse | Sort-Object FullName)
$installLines = [Collections.Generic.List[string]]::new()
$uninstallLines = [Collections.Generic.List[string]]::new()
$inventory = foreach ($file in $files) {
    $relative = $file.FullName.Substring($installedStage.Length + 1)
    if ($relative -match '(^|\\)(profile\.json|prismlauncher\.cfg|chroma-ui\.cfg|accounts\.json|instances|logs|metacache)(\\|$)') { throw "Profile data in release staging: $relative" }
    $directory = Split-Path -Parent $relative
    $installLines.Add('SetOutPath "$INSTDIR' + $(if ($directory) { '\' + (ConvertTo-NsisLiteral $directory) }) + '"')
    $installLines.Add('File "' + (ConvertTo-NsisLiteral $file.FullName) + '"')
    $uninstallLines.Add('Delete "$INSTDIR\' + (ConvertTo-NsisLiteral $relative) + '"')
    [ordered]@{ path = $relative.Replace('\', '/'); size = $file.Length; sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
}
Get-ChildItem -LiteralPath $installedStage -Directory -Recurse | Sort-Object { $_.FullName.Length } -Descending | ForEach-Object {
    $relative = $_.FullName.Substring($installedStage.Length + 1)
    $uninstallLines.Add('RMDir "$INSTDIR\' + (ConvertTo-NsisLiteral $relative) + '"')
}
$installManifest = Join-Path $stagingRoot 'install-files.nsh'
$uninstallManifest = Join-Path $stagingRoot 'uninstall-files.nsh'
Set-Content -LiteralPath $installManifest -Value $installLines -Encoding UTF8
Set-Content -LiteralPath $uninstallManifest -Value $uninstallLines -Encoding UTF8
$installer = Join-Path $releaseRoot "Chroma-$Version-Windows-x64-Setup.exe"
& $NsisCompiler /V3 "/DVERSION=$Version" "/DPROJECT_ROOT=$projectRoot" "/DOUTPUT_FILE=$installer" "/DINSTALL_MANIFEST=$installManifest" "/DUNINSTALL_MANIFEST=$uninstallManifest" (Join-Path $projectRoot 'installer\chroma.nsi')
if ($LASTEXITCODE -ne 0) { throw "NSIS failed: $LASTEXITCODE" }
$zipPath = Join-Path $releaseRoot "Chroma-$Version-Windows-x64.zip"
Compress-Archive -LiteralPath $portableStage -DestinationPath $zipPath -CompressionLevel Optimal -Force
$metadata = [ordered]@{ version = $Version; installedFiles = @($inventory); installer = (Split-Path -Leaf $installer); portableZip = (Split-Path -Leaf $zipPath) }
$metadata | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $releaseRoot 'package-manifest.json') -Encoding UTF8
Get-FileHash -LiteralPath $installer, $zipPath -Algorithm SHA256 | Format-Table -AutoSize
Write-Output "Release staging: $stagingRoot"
