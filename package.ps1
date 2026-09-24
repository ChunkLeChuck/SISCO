<#
.SYNOPSIS
    Packages SISCO for Nexus: the plugin, its settings file, DXVK 3.1.1 as vulkan.dll, and the licences.

.DESCRIPTION
    What goes in the zip, and why:

      plugins\SISCO.asi         the built plugin, taken from obj\ after build.ps1 has passed everything
      plugins\SISCO.ini         the four switches, all on
      vulkan.dll                DXVK 3.1.1's official x32\d3d9.dll, unmodified, under the name FusionFix's
                                D3D9 proxy loads DXVK by. Shipped since 2026-09-23 so that every player runs
                                the DXVK SISCO is tested against; a clean install's slow load was measured as
                                -managed under a cap or VSync, on 2.6.2 and 3.1.1 alike (L4, L5, L6, L10), not the version.
                                It overwrites the copy FusionFix installed. Vendored under external\dxvk\x32,
                                not committed, and pinned by size and MD5 below exactly as DLSS-IV pins the
                                same file.
      README.txt                the README, as text
      LICENSE.txt               SISCO's own licence (MIT)
      licenses\LICENSE-DXVK.txt DXVK's zlib/libpng licence, which redistribution requires

    Refuses to package if anything is missing or the DXVK file is not the pinned one. A wrong or truncated
    vulkan.dll is a game that will not start, with nothing pointing at us.

.PARAMETER Label
    Appended to the zip name, e.g. -Label dev gives SISCO-1.0.1-dev.zip. Use it for anything not for upload.
#>
param([string]$Label = '')

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

# The version is the one the plugin reports, read from the source so the zip can never disagree with the log.
$verLine = Select-String -Path (Join-Path $root 'SIS\SIS.cpp'), (Join-Path $root 'core\sis_core.h') -Pattern '#define\s+SIS_VERSION\s+"([^"]+)"' | Select-Object -First 1
if (-not $verLine) { throw 'SIS_VERSION not found in the sources' }
$version = $verLine.Matches[0].Groups[1].Value

$asi       = Join-Path $root 'obj\SISCO.asi'
$ini       = Join-Path $root 'SISCO.ini'
$readme    = Join-Path $root 'README.md'
$license   = Join-Path $root 'LICENSE'
$dxvkDll   = Join-Path $root 'external\dxvk\x32\d3d9.dll'
$dxvkTerms = Join-Path $root 'external\dxvk\LICENSE-DXVK.txt'
$dxvkBytes = 7856142
$dxvkMd5   = '093943DBADC00725ED39BB25B22C9DFA'      # DXVK 3.1.1 x32\d3d9.dll; 3.1's is the same size, only the hash differs

foreach ($f in @($asi, $ini, $readme, $license, $dxvkDll, $dxvkTerms)) {
    if (-not (Test-Path $f)) { throw "missing: $f" }
}
$dxvkItem = Get-Item $dxvkDll
if ($dxvkItem.Length -ne $dxvkBytes) {
    throw ("DXVK x32\d3d9.dll is {0} bytes, expected {1}. Refusing to package a runtime nobody has checked." -f $dxvkItem.Length, $dxvkBytes)
}
$dxvkHash = (Get-FileHash $dxvkDll -Algorithm MD5).Hash
if ($dxvkHash -ne $dxvkMd5) {
    throw "DXVK x32\d3d9.dll MD5 is $dxvkHash, expected $dxvkMd5. Refusing to package a runtime nobody has checked."
}
# The plugin in obj\ must be the one build.ps1 last passed: build.ps1 refuses to leave a failing build there,
# but a stale obj\ from an edited source would still be older than the source. Refuse that too.
$newestSource = Get-ChildItem (Join-Path $root 'core'), (Join-Path $root 'SIS') -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ((Get-Item $asi).LastWriteTime -lt $newestSource.LastWriteTime) {
    throw ("obj\SISCO.asi is older than {0}. Run build.ps1 first." -f $newestSource.Name)
}

$name  = 'SISCO-' + $version
if ($Label) { $name = $name + '-' + $Label }
$dist  = Join-Path $root 'dist'
$stage = Join-Path $dist ('stage-' + $name)
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $stage 'plugins') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stage 'licenses') -Force | Out-Null

Copy-Item $asi       (Join-Path $stage 'plugins\SISCO.asi')
Copy-Item $ini       (Join-Path $stage 'plugins\SISCO.ini')
Copy-Item $dxvkDll   (Join-Path $stage 'vulkan.dll')
Copy-Item $license   (Join-Path $stage 'LICENSE.txt')
Copy-Item $dxvkTerms (Join-Path $stage 'licenses\LICENSE-DXVK.txt')
# README.md reads fine as text; the name changes so a player without a Markdown viewer opens it. Everything that
# only makes sense inside the repository is left out of the zip's copy: the source line, and the Building and
# Layout sections (owner, 2026-09-24: nothing about GitHub in the zip).
$text = [IO.File]::ReadAllText($readme)
$text = $text.Replace(' This repository is its source.', '')
$text = $text.Replace('See [LICENSE](LICENSE).', 'See LICENSE.txt.')
$text = [regex]::Replace($text, '(?s)## Building\r?\n.*?(?=## Licence)', '')
if ($text -match 'github|repositor|build\.ps1|## Building|## Layout') { throw "README.txt for the zip still mentions the repository." }
[IO.File]::WriteAllText((Join-Path $stage 'README.txt'), $text, (New-Object System.Text.UTF8Encoding($false)))

$zip = Join-Path $dist ($name + '.zip')
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal
Remove-Item $stage -Recurse -Force

Write-Host ("  DXVK 3.1.1 verified: {0:N0} bytes, MD5 {1}; shipped as vulkan.dll" -f $dxvkItem.Length, $dxvkHash) -ForegroundColor Green
Write-Host ("  {0}  ({1:N0} bytes)" -f $zip, (Get-Item $zip).Length) -ForegroundColor Green
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($zip)
try { foreach ($e in $archive.Entries) { Write-Host ("    {0,10:N0}  {1}" -f $e.Length, $e.FullName) } } finally { $archive.Dispose() }
