param(
    [Parameter(Mandatory = $true)][string]$ReleaseDirectory,
    [Parameter(Mandatory = $true)][string]$PreviousInstaller,
    [Parameter(Mandatory = $true)][string]$PreviousZip
)
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$version = & (Join-Path $PSScriptRoot 'get-chroma-version.ps1')
$fixture = Join-Path $project ('.chroma-test\package-update-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
$powershell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$results = @()
foreach ($mode in @('installed', 'portable')) {
    $root = Join-Path $fixture $mode
    New-Item -ItemType Directory -Path $root | Out-Null
    if ($mode -eq 'installed') {
        $app = Join-Path $root 'Application with spaces'
        $setup = Start-Process -FilePath $PreviousInstaller -ArgumentList @('/S', '/NOINTEGRATION', "/D=$app") -PassThru -WindowStyle Hidden
        if (-not $setup.WaitForExit(60000)) { $setup.Kill(); throw 'Owned fixture installer timed out.' }
        if ($setup.ExitCode -ne 0) { throw 'Previous installer failed.' }
        $package = Join-Path $ReleaseDirectory "Chroma-$version-Windows-x64-Setup.exe"
    } else {
        Expand-Archive -LiteralPath $PreviousZip -DestinationPath $root
        $app = Join-Path $root 'Chroma'
        $package = Join-Path $ReleaseDirectory "Chroma-$version-Windows-x64.zip"
    }
    $beforeVersion = (Get-Item -LiteralPath (Join-Path $app 'chroma.exe')).VersionInfo.FileVersion
    $world = Join-Path $app 'instances\fixture\minecraft\saves\example'
    New-Item -ItemType Directory -Path $world -Force | Out-Null
    $preserved = @{}
    foreach ($relative in @('profile.json', 'accounts.json', 'prismlauncher.cfg', 'skin-extras\helmet.skinextra', 'skin-outfits\winter.skinoutfit', 'instances\fixture\minecraft\saves\example\level.dat')) {
        $path = Join-Path $app $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
        [IO.File]::WriteAllText($path, 'Synthetic updater preservation fixture')
        $preserved[$path] = (Get-FileHash -LiteralPath $path).Hash
    }
    $manifest = Join-Path $ReleaseDirectory 'package-manifest.json'
    $request = @{
        mode = $mode; parentPid = 0; applicationDir = $app; packagePath = [IO.Path]::GetFullPath($package)
        packageSha256 = (Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash.ToLowerInvariant()
        manifestPath = [IO.Path]::GetFullPath($manifest); manifestSha256 = (Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash.ToLowerInvariant()
        version = $version; restart = $false; restartArguments = @(); noIntegration = $true
    }
    $requestPath = Join-Path $root 'request.json'
    $request | ConvertTo-Json | Set-Content -LiteralPath $requestPath -Encoding UTF8
    $helper = Join-Path $PSScriptRoot 'apply-chroma-update.ps1'
    $process = Start-Process -FilePath $powershell -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$helper`"", '-RequestPath', "`"$requestPath`"", '-Quiet') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root 'stdout.txt') -RedirectStandardError (Join-Path $root 'stderr.txt')
    if (-not $process.WaitForExit(60000)) { $process.Kill(); throw 'Owned update fixture timed out.' }
    $status = Get-Content -LiteralPath (Join-Path $root 'update-result.json') -Raw | ConvertFrom-Json
    if ($process.ExitCode -ne 0 -or -not $status.success) { throw "Package update failed: $($status | ConvertTo-Json -Compress)" }
    foreach ($path in $preserved.Keys) {
        if ((Get-FileHash -LiteralPath $path).Hash -ne $preserved[$path]) { throw "Update changed a profile: $path" }
    }
    $exe = Join-Path $app 'chroma.exe'
    if ((Get-Item -LiteralPath $exe).VersionInfo.FileVersion -ne "$version.0") { throw 'Updated executable has wrong version.' }
    if ((Get-FileHash -LiteralPath $exe).Hash -ne (Get-FileHash -LiteralPath (Join-Path $project '.tools\build\chroma.exe')).Hash) { throw 'Updated executable differs from the build.' }
    if ([IO.File]::ReadAllText((Join-Path $app 'update\installed-version.txt')) -ne $version) { throw 'Full installed version was not recorded.' }
    if ((Test-Path -LiteralPath (Join-Path $app 'portable.txt')) -ne ($mode -eq 'portable')) { throw 'Update changed profile mode.' }
    $results += @{ mode = $mode; fromVersion = $beforeVersion; toVersion = "$version.0"; preservation = 'passed'; executableMatches = $true }
}
@{ passed = 2; results = $results; fixture = $fixture } | ConvertTo-Json -Depth 5
