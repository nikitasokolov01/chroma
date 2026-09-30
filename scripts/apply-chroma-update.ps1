param([Parameter(Mandatory = $true)][string]$RequestPath, [switch]$Quiet)
$ErrorActionPreference = 'Stop'

function Get-Sha256([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}

function Get-SafeRelativePath([string]$Path) {
    $relative = $Path.Replace('\', '/')
    if ([string]::IsNullOrWhiteSpace($relative) -or [IO.Path]::IsPathRooted($relative)) { throw "Invalid package path: $Path" }
    foreach ($part in $relative.Split('/')) {
        if ($part -in @('', '.', '..') -or $part -match '[<>:"|?*\x00-\x1f]' -or $part -match '[ .]$' -or
            $part -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)') { throw "Invalid package path: $Path" }
    }
    if ($relative -match '^(?i:instances|logs|metacache|cache|skins|accounts|downloads|assets|libraries|java)(/|$)' -or
        $relative -match '(?i)(^|/)(accounts\.json|profile\.json|prismlauncher\.cfg|chroma-ui\.cfg|chroma_update\.cfg|chroma-updates\.ini|chroma-no-integration|Uninstall\.exe)$') {
        throw "Package attempts to replace profile or installation data: $Path"
    }
    return $relative
}

function Get-ChildPath([string]$Root, [string]$Relative) {
    $full = [IO.Path]::GetFullPath((Join-Path $Root $Relative))
    if (-not $full.StartsWith($Root.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Package path leaves application directory.' }
    # Existing junctions/symlinks must never redirect a write into a profile.
    $cursor = $full
    while ($cursor -and $cursor.Length -ge $Root.Length) {
        if (Test-Path -LiteralPath $cursor) {
            if ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Update path contains a link: $cursor" }
        }
        if ($cursor -eq $Root) { break }
        $cursor = Split-Path -Parent $cursor
    }
    return $full
}

function Restart-Chroma {
    if (-not $request.restart) { return }
    # Quote Windows argv, including a profile path with spaces and trailing slashes.
    $arguments = foreach ($arg in @($request.restartArguments)) {
        '"' + ([regex]::Replace([regex]::Replace([string]$arg, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1')) + '"'
    }
    $start = @{ FilePath = (Join-Path $application 'chroma.exe'); WorkingDirectory = $application }
    if (@($arguments).Count) { $start.ArgumentList = $arguments -join ' ' }
    Start-Process @start | Out-Null
}

$requestFile = [IO.Path]::GetFullPath($RequestPath)
$staging = Split-Path -Parent $requestFile
$statusFile = Join-Path $staging 'update-result.json'
$changed = [Collections.Generic.List[object]]::new()
$request = $null
$application = $null
try {
    $request = Get-Content -LiteralPath $requestFile -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($request.mode -notin @('portable', 'installed') -or $request.version -notmatch '^\d+\.\d+\.\d+(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?\z') { throw 'Invalid update request.' }
    $application = [IO.Path]::GetFullPath([string]$request.applicationDir).TrimEnd('\')
    if (-not (Test-Path -LiteralPath (Join-Path $application 'chroma.exe') -PathType Leaf)) { throw 'The selected installation has no chroma.exe.' }
    [void](Get-ChildPath $application 'chroma.exe')
    $portable = Test-Path -LiteralPath (Join-Path $application 'portable.txt')
    if ($portable -ne ($request.mode -eq 'portable')) { throw 'Update package does not match the installation type.' }
    if ($request.packageSha256 -notmatch '^[a-fA-F0-9]{64}$' -or (Get-Sha256 $request.packagePath) -ne $request.packageSha256) { throw 'Update package checksum mismatch.' }
    if ($request.manifestSha256 -notmatch '^[a-fA-F0-9]{64}$' -or (Get-Sha256 $request.manifestPath) -ne $request.manifestSha256) { throw 'Package manifest checksum mismatch.' }
    $manifest = Get-Content -LiteralPath $request.manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($manifest.version -ne $request.version) { throw 'Package manifest version does not match the release.' }

    if ($request.mode -eq 'portable') {
        if (-not $manifest.portableFiles) { throw 'Package portable inventory is missing.' }
        $expected = @{}
        foreach ($file in $manifest.portableFiles) {
            $relative = Get-SafeRelativePath $file.path
            if ($expected.ContainsKey($relative) -or $file.sha256 -notmatch '^[a-fA-F0-9]{64}$' -or [long]$file.size -lt 0) { throw 'Invalid or duplicate package manifest entry.' }
            [void](Get-ChildPath $application $relative)
            $expected[$relative] = $file
        }
        if (-not $expected.ContainsKey('chroma.exe') -or -not $expected.ContainsKey('portable.txt')) { throw 'Portable launcher files are missing from the manifest.' }
        $expanded = Join-Path $staging ('expanded-' + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $expanded | Out-Null
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $archive = [IO.Compression.ZipFile]::OpenRead($request.packagePath)
        try {
            $seen = @{}
            foreach ($entry in $archive.Entries) {
                $name = $entry.FullName.Replace('\', '/')
                if ($name.EndsWith('/')) { continue }
                if (-not $name.StartsWith('Chroma/', [StringComparison]::Ordinal)) { throw 'Unexpected archive root.' }
                $relative = Get-SafeRelativePath $name.Substring(7)
                if (-not $expected.ContainsKey($relative) -or $seen.ContainsKey($relative) -or $entry.Length -ne [long]$expected[$relative].size -or
                    (($entry.ExternalAttributes -shr 16) -band 0xf000) -eq 0xa000) { throw "Unexpected archive entry: $relative" }
                $destination = Get-ChildPath $expanded $relative
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
                [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $destination)
                if ((Get-Sha256 $destination) -ne $expected[$relative].sha256) { throw "Package file checksum mismatch: $relative" }
                $seen[$relative] = $true
            }
            if ($seen.Count -ne $expected.Count) { throw 'Archive is missing files listed in the manifest.' }
        } finally { $archive.Dispose() }
    }

    # Wait for the launcher to save its settings and release every DLL.
    if ([int]$request.parentPid -gt 0) {
        $parentProcess = Get-Process -Id ([int]$request.parentPid) -ErrorAction SilentlyContinue
        if ($parentProcess -and -not $parentProcess.WaitForExit(120000)) { throw 'Chroma did not close. The update was not applied.' }
    }
    foreach ($process in @(Get-Process -Name chroma -ErrorAction SilentlyContinue)) {
        if ($process.Path -eq (Join-Path $application 'chroma.exe')) { throw 'Another Chroma window is using this installation. Close it before updating.' }
    }

    if ($request.mode -eq 'installed') {
        $installerArguments = @('/S')
        if ($request.noIntegration) { $installerArguments += '/NOINTEGRATION' }
        $installerArguments += "/D=$application"
        $installer = Start-Process -FilePath $request.packagePath -ArgumentList $installerArguments -PassThru -WindowStyle Hidden
        if (-not $installer.WaitForExit(120000)) { throw 'Installer is still running. Check it before restarting Chroma.' }
        if ($installer.ExitCode -ne 0) { throw "Installer failed with code $($installer.ExitCode)." }
    } else {
        $backup = Join-Path $staging ('backup-' + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $backup | Out-Null
        foreach ($relative in @($expected.Keys | Sort-Object)) {
            # Keep the existing portable marker and every file not in the inventory.
            if ($relative -eq 'portable.txt') { continue }
            $destination = Get-ChildPath $application $relative
            $source = Get-ChildPath $expanded $relative
            $saved = Get-ChildPath $backup $relative
            $existed = Test-Path -LiteralPath $destination
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
            if ($existed) {
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $saved) | Out-Null
                [IO.File]::Copy($destination, $saved)
            }
            $incoming = $destination + '.chroma-update-' + [guid]::NewGuid().ToString('N')
            try {
                [IO.File]::Copy($source, $incoming)
                if ($existed) { [IO.File]::Replace($incoming, $destination, [NullString]::Value) }
                else { [IO.File]::Move($incoming, $destination) }
                $changed.Add(@{ path = $destination; backup = $saved; existed = $existed })
            } finally { if (Test-Path -LiteralPath $incoming) { Remove-Item -LiteralPath $incoming } }
        }
    }
    $versionFile = Get-ChildPath $application 'update/installed-version.txt'
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $versionFile) | Out-Null
    if ($request.mode -eq 'portable') {
        $savedVersion = Join-Path $backup 'previous-installed-version.txt'
        $hadVersion = Test-Path -LiteralPath $versionFile
        if ($hadVersion) { [IO.File]::Copy($versionFile, $savedVersion) }
        $changed.Add(@{ path = $versionFile; backup = $savedVersion; existed = $hadVersion })
    }
    [IO.File]::WriteAllText($versionFile, $request.version, [Text.UTF8Encoding]::new($false))
    @{ success = $true; version = $request.version } | ConvertTo-Json | Set-Content -LiteralPath $statusFile -Encoding UTF8
    Restart-Chroma
} catch {
    $failure = $_.Exception.Message
    $rollbackErrors = @()
    for ($index = $changed.Count - 1; $index -ge 0; $index--) {
        $file = $changed[$index]
        try {
            if ($file.existed) { [IO.File]::Copy($file.backup, $file.path, $true) }
            else { Remove-Item -LiteralPath $file.path }
        } catch { $rollbackErrors += $_.Exception.Message }
    }
    @{ success = $false; error = $failure; rollbackErrors = $rollbackErrors } | ConvertTo-Json | Set-Content -LiteralPath $statusFile -Encoding UTF8
    if (-not $Quiet) {
        Add-Type -AssemblyName System.Windows.Forms
        [void][Windows.Forms.MessageBox]::Show("$failure`n`nDetails: $statusFile", 'Chroma update failed')
    }
    Write-Error $failure -ErrorAction Continue
    exit 1
}
