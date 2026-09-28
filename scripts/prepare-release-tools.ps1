param([string]$SevenZip = 'C:\Program Files\7-Zip\7z.exe')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$toolsRoot = Join-Path $projectRoot '.tools'
$downloads = Join-Path $toolsRoot 'release-downloads'
New-Item -ItemType Directory -Force -Path $downloads | Out-Null
function Get-VerifiedDownload([string]$Url, [string]$Name, [string]$Sha256) {
    $path = Join-Path $downloads $Name
    if (-not (Test-Path -LiteralPath $path)) {
        Invoke-WebRequest -Uri $Url -OutFile $path
    }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $Sha256) { throw "SHA256 mismatch: $path. Review upstream changes before updating the pinned hash." }
    return $path
}
$nsis = Get-VerifiedDownload 'https://downloads.sourceforge.net/project/nsis/NSIS%203/3.12/nsis-3.12.zip' 'nsis-3.12.zip' '56581F90DB321581C5381193D796FFFCF2D24B2F8FED2160A6C6A3BAA67F2C4F'
Expand-Archive -LiteralPath $nsis -DestinationPath (Join-Path $toolsRoot 'nsis-3.12-portable') -Force
$runtime = Get-VerifiedDownload 'https://aka.ms/vs/17/release/vc_redist.x64.exe' 'vc_redist.x64.exe' 'CC0FF0EB1DC3F5188AE6300FAEF32BF5BEEBA4BDD6E8E445A9184072096B713B'
$signature = Get-AuthenticodeSignature -LiteralPath $runtime
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') { throw 'Microsoft runtime signature verification failed.' }
if (-not (Test-Path -LiteralPath $SevenZip)) { throw 'Provide -SevenZip pointing to 7z.exe for extraction.' }

# Extract signed Burn/CAB payloads as data; never run a runtime installer.
$extractRoot = Join-Path $downloads ('runtime-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $extractRoot | Out-Null
$bytes = [IO.File]::ReadAllBytes($runtime)
$search = [Text.Encoding]::GetEncoding(28591).GetString($bytes)
$marker = 'MSCF' + [char]0 + [char]0 + [char]0 + [char]0
$offset = -1
$cabNumber = 0
while (($offset = $search.IndexOf($marker, $offset + 1, [StringComparison]::Ordinal)) -ge 0) {
    $size = [BitConverter]::ToUInt32($bytes, $offset + 8)
    if ($size -lt 36 -or $offset + $size -gt $bytes.Length) { continue }
    $cab = Join-Path $extractRoot "$cabNumber.cab"
    $cabBytes = [byte[]]::new($size)
    [Array]::Copy($bytes, $offset, $cabBytes, 0, $size)
    [IO.File]::WriteAllBytes($cab, $cabBytes)
    $out = Join-Path $extractRoot "cab-$cabNumber"
    & $SevenZip x $cab "-o$out" -y | Out-Null
    if ($LASTEXITCODE -gt 1) { throw 'Runtime CAB extraction failed.' }
    $cabNumber++
}
$runtimeRoot = Join-Path $toolsRoot 'release-runtime'
New-Item -ItemType Directory -Force -Path $runtimeRoot | Out-Null
foreach ($payload in Get-ChildItem -LiteralPath $extractRoot -File -Recurse) {
    if ($payload.Name -notmatch '^a\d+$') { continue }
    $listing = & $SevenZip l $payload.FullName 2>$null
    if ($listing -notmatch 'msvcp140\.dll_amd64') { continue }
    $crtRoot = Join-Path $extractRoot 'crt'
    & $SevenZip x $payload.FullName "-o$crtRoot" -y | Out-Null
    if ($LASTEXITCODE -gt 1) { throw 'Runtime CRT extraction failed.' }
    Get-ChildItem -LiteralPath $crtRoot -Filter '*.dll_amd64' -File |
        Where-Object Name -Match '^(msvcp|vcruntime|concrt)' | ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $runtimeRoot ($_.Name -replace '_amd64$', '')) -Force
        }
}
foreach ($name in @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')) {
    $dll = Join-Path $runtimeRoot $name
    if (-not (Test-Path -LiteralPath $dll) -or (Get-Item -LiteralPath $dll).VersionInfo.FileVersion -notlike '14.44.35211*') { throw "Expected runtime 14.44.35211: $dll" }
}
Write-Output "NSIS 3.12 and app-local VC runtime 14.44.35211 are ready under $toolsRoot."
