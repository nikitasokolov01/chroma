param()
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$fixture = Join-Path $project ('.chroma-test\updater-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
$helper = Join-Path $PSScriptRoot 'apply-chroma-update.ps1'
$powershell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'

function New-TestCase([string]$Name) {
    $root = Join-Path $fixture $Name
    $app = Join-Path $root 'Chroma app with spaces'
    $payload = Join-Path $root 'payload\Chroma'
    New-Item -ItemType Directory -Force -Path $app, $payload, (Join-Path $app 'instances\example\minecraft\saves') | Out-Null
    [IO.File]::WriteAllText((Join-Path $app 'chroma.exe'), 'old binary')
    [IO.File]::WriteAllText((Join-Path $app 'z-last.dll'), 'old library')
    [IO.File]::WriteAllText((Join-Path $app 'portable.txt'), 'keep marker')
    [IO.File]::WriteAllText((Join-Path $app 'profile.json'), '{"keep":"profile"}')
    [IO.File]::WriteAllText((Join-Path $app 'accounts.json'), '{"keep":"account"}')
    New-Item -ItemType Directory -Path (Join-Path $app 'skin-extras') | Out-Null
    [IO.File]::WriteAllText((Join-Path $app 'skin-extras\helmet.skinextra'), 'keep helmet')
    New-Item -ItemType Directory -Path (Join-Path $app 'skin-outfits') | Out-Null
    [IO.File]::WriteAllText((Join-Path $app 'skin-outfits\winter.skinoutfit'), 'keep outfit')
    [IO.File]::WriteAllText((Join-Path $app 'instances\example\minecraft\saves\level.dat'), 'keep world')
    [IO.File]::WriteAllText((Join-Path $payload 'chroma.exe'), 'new binary')
    [IO.File]::WriteAllText((Join-Path $payload 'z-last.dll'), 'new library')
    [IO.File]::WriteAllText((Join-Path $payload 'portable.txt'), 'new marker')
    $zip = Join-Path $root 'update.zip'
    Compress-Archive -LiteralPath $payload -DestinationPath $zip
    $files = @(Get-ChildItem -LiteralPath $payload -File | ForEach-Object {
        @{ path = $_.Name; size = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
    $manifest = Join-Path $root 'package-manifest.json'
    @{ version = '0.3.0'; portableFiles = $files } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifest -Encoding UTF8
    $request = @{
        mode = 'portable'; parentPid = 0; applicationDir = $app; packagePath = $zip
        packageSha256 = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
        manifestPath = $manifest; manifestSha256 = (Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash.ToLowerInvariant()
        version = '0.3.0'; restart = $false; restartArguments = @()
    }
    return @{ root = $root; app = $app; request = $request }
}

function Invoke-TestCase($Case, [bool]$Success, [bool]$BomlessUtf8 = $false) {
    $requestPath = Join-Path $Case.root 'request.json'
    $requestJson = $Case.request | ConvertTo-Json -Depth 6
    if ($BomlessUtf8) {
        # Match QJsonDocument/QSaveFile: UTF-8 bytes without a BOM, including literal Unicode paths.
        [IO.File]::WriteAllText($requestPath, $requestJson, [Text.UTF8Encoding]::new($false))
    } else {
        $requestJson | Set-Content -LiteralPath $requestPath -Encoding UTF8
    }
    $process = Start-Process -FilePath $powershell -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$helper`"", '-RequestPath', "`"$requestPath`"", '-Quiet') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $Case.root 'stdout.txt') -RedirectStandardError (Join-Path $Case.root 'stderr.txt')
    if (-not $process.WaitForExit(30000)) { $process.Kill(); throw 'Owned update test process timed out.' }
    $status = Get-Content -LiteralPath (Join-Path $Case.root 'update-result.json') -Raw | ConvertFrom-Json
    if ($status.success -ne $Success -or (($process.ExitCode -eq 0) -ne $Success)) { throw "Unexpected update result: $($status | ConvertTo-Json -Compress)" }
    if ($status.rollbackErrors.Count) { throw 'Rollback reported errors.' }
    $expectedBinary = if ($Success) { 'new binary' } else { 'old binary' }
    if ([IO.File]::ReadAllText((Join-Path $Case.app 'chroma.exe')) -ne $expectedBinary) { throw 'Unexpected executable contents.' }
    foreach ($pair in @(@('portable.txt', 'keep marker'), @('profile.json', '{"keep":"profile"}'), @('accounts.json', '{"keep":"account"}'), @('skin-extras\helmet.skinextra', 'keep helmet'), @('skin-outfits\winter.skinoutfit', 'keep outfit'), @('instances\example\minecraft\saves\level.dat', 'keep world'))) {
        if ([IO.File]::ReadAllText((Join-Path $Case.app $pair[0])) -ne $pair[1]) { throw "Profile was changed: $($pair[0])" }
    }
}

$success = New-TestCase 'success'
Invoke-TestCase $success $true
$unicode = New-TestCase ('unicode-' + [char]0x00e9 + [char]0x4e2d)
Invoke-TestCase $unicode $true $true
$checksum = New-TestCase 'bad-checksum'
$checksum.request.packageSha256 = '0' * 64
Invoke-TestCase $checksum $false
$manifestHash = New-TestCase 'bad-manifest-hash'
$manifestHash.request.manifestSha256 = '0' * 64
Invoke-TestCase $manifestHash $false
$profile = New-TestCase 'profile-entry'
$manifest = Get-Content -LiteralPath $profile.request.manifestPath -Raw | ConvertFrom-Json
$manifest.portableFiles += @{ path = 'accounts.json'; size = 0; sha256 = '0' * 64 }
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $profile.request.manifestPath -Encoding UTF8
$profile.request.manifestSha256 = (Get-FileHash -LiteralPath $profile.request.manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
Invoke-TestCase $profile $false
$extras = New-TestCase 'skin-extras-entry'
$manifest = Get-Content -LiteralPath $extras.request.manifestPath -Raw | ConvertFrom-Json
$manifest.portableFiles += @{ path = 'skin-extras/helmet.skinextra'; size = 0; sha256 = '0' * 64 }
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $extras.request.manifestPath -Encoding UTF8
$extras.request.manifestSha256 = (Get-FileHash -LiteralPath $extras.request.manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
Invoke-TestCase $extras $false
$outfits = New-TestCase 'skin-outfits-entry'
$manifest = Get-Content -LiteralPath $outfits.request.manifestPath -Raw | ConvertFrom-Json
$manifest.portableFiles += @{ path = 'skin-outfits/winter.skinoutfit'; size = 0; sha256 = '0' * 64 }
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $outfits.request.manifestPath -Encoding UTF8
$outfits.request.manifestSha256 = (Get-FileHash -LiteralPath $outfits.request.manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
Invoke-TestCase $outfits $false
$traversal = New-TestCase 'path-traversal'
$manifest = Get-Content -LiteralPath $traversal.request.manifestPath -Raw | ConvertFrom-Json
$manifest.portableFiles += @{ path = '../outside.exe'; size = 0; sha256 = '0' * 64 }
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $traversal.request.manifestPath -Encoding UTF8
$traversal.request.manifestSha256 = (Get-FileHash -LiteralPath $traversal.request.manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
Invoke-TestCase $traversal $false
$rollback = New-TestCase 'rollback'
$locked = [IO.File]::Open((Join-Path $rollback.app 'z-last.dll'), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
try { Invoke-TestCase $rollback $false } finally { $locked.Dispose() }
if ([IO.File]::ReadAllText((Join-Path $rollback.app 'z-last.dll')) -ne 'old library') { throw 'Locked library was changed.' }
@{ passed = 9; bomlessUnicodeRequest = $true; profilePreservation = $true; skinExtrasPreservation = $true; skinOutfitsPreservation = $true; rollback = $true; fixture = $fixture } | ConvertTo-Json
