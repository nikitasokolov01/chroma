param([Parameter(Mandatory = $true)][string]$ReleaseDirectory)
$ErrorActionPreference = 'Stop'
$releaseRoot = (Resolve-Path -LiteralPath $ReleaseDirectory).Path
if (-not (Test-Path -LiteralPath $releaseRoot -PathType Container)) { throw 'Choose a release directory.' }
$checksumPath = Join-Path $releaseRoot 'SHA256SUMS.txt'
$files = @(Get-ChildItem -LiteralPath $releaseRoot -File | Where-Object { $_.Name -ne 'SHA256SUMS.txt' -and $_.Name -notlike '.checksums-*' } | Sort-Object Name)
if (-not $files.Count) { throw 'The release directory contains no artifacts to checksum.' }
$lines = foreach ($file in $files) {
    if ($file.Name -match '[\r\n]') { throw 'Release artifact names must not contain line breaks.' }
    (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $file.Name
}
$temporary = Join-Path $releaseRoot ('.checksums-' + [guid]::NewGuid().ToString('N'))
try {
    [IO.File]::WriteAllText($temporary, (($lines -join "`n") + "`n"), [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temporary -Destination $checksumPath -Force
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
}
Write-Output "Release checksums: $checksumPath"
