param(
    [ValidateSet('Environment', 'Configure', 'Build', 'Test', 'All')][string]$Action = 'All',
    [string]$QtRoot,
    [string]$VcpkgRoot,
    [string]$JavaHome,
    [string]$VcVars,
    [int]$Jobs = 6,
    [switch]$ReleaseBuild,
    [string]$MicrosoftClientId = $env:CHROMA_MSA_CLIENT_ID,
    [string]$CurseForgeApiKey = $env:CHROMA_CURSEFORGE_API_KEY
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$toolRoot = Join-Path $projectRoot '.tools'
$buildRoot = Join-Path $toolRoot 'build'
if (-not $QtRoot) { $QtRoot = Join-Path $toolRoot 'Qt\6.5.3\msvc2019_64' }
if (-not $VcpkgRoot) { $VcpkgRoot = Join-Path $toolRoot 'vcpkg' }
if (-not $JavaHome) { $JavaHome = $env:JAVA_HOME }
if (-not $JavaHome) { $JavaHome = 'C:\Program Files\Java\jdk-17.0.2' }
if (-not $VcVars) { $VcVars = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat' }
foreach ($required in @($QtRoot, $VcpkgRoot, $JavaHome, $VcVars)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing prerequisite: $required. See docs/CHROMA.md." }
}
New-Item -ItemType Directory -Force -Path $toolRoot | Out-Null
$envScript = Join-Path $toolRoot 'chroma-vs-environment.cmd'
Set-Content -LiteralPath $envScript -Encoding ASCII -Value @('@echo off', ('call "' + $VcVars + '" >nul'), 'if errorlevel 1 exit /b 1', 'set')
$environmentLines = & $env:ComSpec /c $envScript
if ($LASTEXITCODE -ne 0) { throw 'Visual Studio environment initialization failed.' }
foreach ($line in $environmentLines) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$env:JAVA_HOME = $JavaHome
$env:VCPKG_ROOT = $VcpkgRoot
$env:VCPKG_DEFAULT_BINARY_CACHE = Join-Path $toolRoot 'vcpkg-cache'
$env:VCPKG_DOWNLOADS = Join-Path $toolRoot 'vcpkg-downloads'
$env:X_VCPKG_REGISTRIES_CACHE = Join-Path $toolRoot 'vcpkg-registries-cache'
New-Item -ItemType Directory -Force -Path $env:VCPKG_DEFAULT_BINARY_CACHE, $env:VCPKG_DOWNLOADS, $env:X_VCPKG_REGISTRIES_CACHE | Out-Null
$installed = Join-Path $toolRoot 'vcpkg-installed'
$env:PATH = "$(Join-Path $toolRoot 'python\Scripts');$QtRoot\bin;$installed\x64-windows\bin;$JavaHome\bin;C:\Program Files\Git\usr\bin;C:\Program Files\Git\mingw64\bin;$env:PATH"
if ($Action -in @('Configure', 'All')) {
    # Local UI-overhaul builds retain Prism's public Microsoft OAuth client ID.
    # An explicit parameter (including an empty string) or environment override wins.
    $msaClientArgument = '-ULauncher_MSA_CLIENT_ID'
    if ($PSBoundParameters.ContainsKey('MicrosoftClientId') -or -not [string]::IsNullOrEmpty($MicrosoftClientId)) {
        $msaClientArgument = "-DLauncher_MSA_CLIENT_ID=$MicrosoftClientId"
    }
    $releaseBuildOption = if ($ReleaseBuild) { 'ON' } else { 'OFF' }
    $buildPlatform = if ($ReleaseBuild) { 'chroma-windows-x64' } else { 'chroma-development' }
    & cmake -S $projectRoot -B $buildRoot -G Ninja '-DCMAKE_BUILD_TYPE=RelWithDebInfo' `
        "-DCMAKE_PREFIX_PATH=$QtRoot" "-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot\scripts\buildsystems\vcpkg.cmake" `
        "-DVCPKG_INSTALLED_DIR=$installed" '-DVCPKG_TARGET_TRIPLET=x64-windows' `
        '-DBUILD_TESTING=ON' '-DENABLE_LTO=OFF' '-DLauncher_USE_PCH=ON' `
        "-DLauncher_RELEASE_BUILD=$releaseBuildOption" `
        "-DLauncher_BUILD_PLATFORM=$buildPlatform" '-DLauncher_BUILD_ARTIFACT=' `
        '-DLauncher_UPDATER_GITHUB_REPO=' '-DLauncher_APP_BINARY_NAME=chroma' `
        '-DLauncher_BUG_TRACKER_URL=https://github.com/nikitasokolov01/chroma/issues' `
        $msaClientArgument "-DLauncher_CURSEFORGE_API_KEY=$CurseForgeApiKey" '-DLauncher_IMGUR_CLIENT_ID='
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
if ($Action -in @('Build', 'All')) {
    & cmake --build $buildRoot --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
if ($Action -in @('Test', 'All')) {
    # Git on Windows may check out directory symlinks as text files. Resolve the
    # two fixture links in the build tree for QFINDTESTDATA; leave source untouched.
    $fixtureLinks = @{ Library = 'MojangVersionFormat'; ResourceFolderModel = 'ResourcePackParse' }
    foreach ($testDataRoot in @((Join-Path $buildRoot 'testdata'), (Join-Path $buildRoot 'tests\testdata'))) {
        foreach ($fixtureName in $fixtureLinks.Keys) {
            $fixtureTarget = Join-Path $testDataRoot $fixtureName
            New-Item -ItemType Directory -Force -Path $fixtureTarget | Out-Null
            Get-ChildItem -LiteralPath (Join-Path $projectRoot ('tests\testdata\' + $fixtureLinks[$fixtureName])) -Force |
                Copy-Item -Destination $fixtureTarget -Recurse -Force
        }
    }
    & ctest --test-dir $buildRoot --output-on-failure --parallel $Jobs -E '^example64|example$'
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
