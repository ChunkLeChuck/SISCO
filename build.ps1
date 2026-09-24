<#
.SYNOPSIS
    Builds SISCO.asi (x86) from core\sis_core.h, runs its tests and its checks, and optionally deploys it.

.DESCRIPTION
    Nothing is deployed unless every step passes:
      1. the .asi and the test exe build, and both are x86 (a 64-bit .asi loads in nothing and the only symptom
         is silence),
      2. the core's tests pass in the release's own compile, so what they exercise is the code that ships,
      3. the real .asi, loaded into a process that is not the game, changes nothing and says so,
      4. every address and every expected byte still matches the game on disk, for each game it can find,
      5. no instrument has found its way back into the source.

    Step 4 needs a copy of the game to check against, and it says out loud when it has none, because a check that
    skips itself in silence is worse than no check. Point it at yours with -Game1080 and -GameCE, or put the paths
    in games.txt beside this script, one "name = path" per line. The Complete Edition ships its first megabyte
    encrypted, so its check reads a copy recovered from a runtime dump, not the executable as installed
    (docs\CE-SITES.md).

.EXAMPLE
    .\build.ps1
    .\build.ps1 -Game1080 'D:\GTAIV\GTAIV.exe'
    .\build.ps1 -Deploy '<game folder>'      copies SISCO.asi into <folder>\plugins
#>
[CmdletBinding()]
param([string]$Deploy, [string]$Game1080, [string]$GameCE)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$obj = Join-Path $root 'obj'
New-Item -ItemType Directory -Force $obj | Out-Null

# The compiler is found, not assumed: any edition, any year, wherever it was installed.
$vcvars = $null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $vswhere) {
    $vcvars = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                         -find 'VC\Auxiliary\Build\vcvars32.bat' | Select-Object -First 1
}
if (-not $vcvars -or -not (Test-Path $vcvars)) {
    $vcvars = Get-ChildItem 'C:\Program Files*\Microsoft Visual Studio\*\*\VC\Auxiliary\Build\vcvars32.bat' -ErrorAction SilentlyContinue |
              Select-Object -First 1 -ExpandProperty FullName
}
if (-not $vcvars) { throw "No Visual Studio C++ build tools found. Install the Desktop C++ workload, or pass vcvars32.bat by hand." }

# Python writes warnings to stderr, and under 'Stop' a native command's stderr is a terminating error. One deprecation
# notice from an unrelated library would otherwise kill this build with a message about nothing.
if (-not (Get-Command python -ErrorAction SilentlyContinue)) { throw "python is not on PATH: the site checks cannot run." }
function Invoke-Checker([string[]]$checkerArgs) {
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $out = & python @checkerArgs 2>&1; $code = $LASTEXITCODE } finally { $ErrorActionPreference = $old }
    return @{ out = $out; code = $code }
}

# Build id: the commit, plus -dirty when the tree has changes, so a log names exactly what ran.
$commit = (git -C $root rev-parse --short=8 HEAD).Trim()
$dirty = (git -C $root status --porcelain -- .).Length -gt 0
$buildId = if ($dirty) { "$commit-dirty" } else { $commit }
Set-Content -Path (Join-Path $obj 'sis_build.h') -Value "#define SISCO_BUILD `"$buildId`"" -Encoding ascii

# /MT: carries its own CRT. /GS-: no security cookie. /Oy-: frames stay walkable in a crash dump.
$common = "/nologo /std:c++17 /O2 /Oy- /MT /GS- /EHa /W3 /DWIN32 /D_WINDOWS /I`"$obj`" /FI`"$obj\sis_build.h`""

function Invoke-Cl([string]$clArgs, [string]$what) {
    $cmd = "`"$vcvars`" >nul 2>&1 && cl $clArgs"
    $out = & cmd.exe /c $cmd 2>&1
    $out | Where-Object { $_ -match 'error|warning' } | ForEach-Object { Write-Host "  $_" }
    if ($LASTEXITCODE -ne 0) { $out | ForEach-Object { Write-Host $_ }; throw "$what failed." }
}

Write-Host "Building SISCO.asi ($buildId, x86)..." -ForegroundColor Cyan
$sis = Join-Path $obj 'SISCO.asi'
Invoke-Cl "$common /LD `"$root\SIS\SIS.cpp`" /Fe:`"$sis`" /Fo:`"$obj\SIS.obj`" /link /SUBSYSTEM:WINDOWS /MACHINE:X86 /INCREMENTAL:NO" 'SISCO.asi build'

Write-Host "Building the test exe..." -ForegroundColor Cyan
$test = Join-Path $obj 'sis_test.exe'
Invoke-Cl "$common `"$root\SIS\sis_test.cpp`" /Fe:`"$test`" /Fo:`"$obj\sis_test.obj`" /link /SUBSYSTEM:CONSOLE /MACHINE:X86 /INCREMENTAL:NO" 'sis_test.exe build'

foreach ($f in @($sis, $test)) {
    $b = [System.IO.File]::ReadAllBytes($f)
    $pe = [BitConverter]::ToInt32($b, 0x3C)
    $machine = [BitConverter]::ToUInt16($b, $pe + 4)
    if ($machine -ne 0x014C) { throw "$f is not x86 (machine 0x$($machine.ToString('X4')))." }
}
Write-Host ("  SISCO.asi {0:N0} bytes, x86" -f (Get-Item $sis).Length) -ForegroundColor Green

Write-Host "Running the core's tests on the release's own compile..." -ForegroundColor Cyan
Push-Location $obj
try { $testOut = & $test; $code = $LASTEXITCODE } finally { Pop-Location }
if ($code -ne 0) { $testOut | ForEach-Object { Write-Host $_ }; throw "Tests FAILED (exit $code). Nothing deployed." }
Write-Host "  $(@($testOut)[-1])" -ForegroundColor Green

Write-Host "Loading SISCO.asi into a process that is not the game..." -ForegroundColor Cyan
$load = Join-Path $obj 'sis_load.exe'
Invoke-Cl "/nologo /O2 /MT `"$root\SIS\sis_load.cpp`" /Fe:`"$load`" /Fo:`"$obj\sis_load.obj`" /link /MACHINE:X86 /INCREMENTAL:NO" 'sis_load.exe build'
$loadOut = & $load $sis
if ($LASTEXITCODE -ne 0) { $loadOut | ForEach-Object { Write-Host $_ }; throw "Load test FAILED. Nothing deployed." }
Write-Host "  passive outside the game, as it must be" -ForegroundColor Green

# Where the two games are: the switches first, then games.txt beside this script, and nothing hard-coded anywhere.
$games = @{ '1.0.8.0' = $Game1080; 'the Complete Edition' = $GameCE }
$list = Join-Path $root 'games.txt'
if (Test-Path $list) {
    foreach ($line in Get-Content $list) {
        if ($line -match '^\s*([^#=][^=]*?)\s*=\s*(.+?)\s*$' -and -not $games[$matches[1]]) { $games[$matches[1]] = $matches[2] }
    }
}
foreach ($name in @('1.0.8.0', 'the Complete Edition')) {
    $exe = $games[$name]
    if (-not $exe -or -not (Test-Path $exe)) {
        Write-Host "  SKIPPED: $name is not configured, so its sites were NOT checked" -ForegroundColor Yellow
        continue
    }
    Write-Host "Checking every site against $name..." -ForegroundColor Cyan
    $r = Invoke-Checker @((Join-Path $root 'tools\sisco_sites_check.py'), $exe, (Join-Path $root 'core\sis_core.h'))
    if ($r.code -ne 0) { $r.out | ForEach-Object { Write-Host $_ }; throw "Site check FAILED for $name. Nothing deployed." }
    Write-Host "  $(@($r.out)[-1])" -ForegroundColor Green
}

Write-Host "Checking no instrument has come back..." -ForegroundColor Cyan
$r = Invoke-Checker @((Join-Path $root 'tools\sis_release_check.py'), $root)
if ($r.code -ne 0) { $r.out | ForEach-Object { Write-Host $_ }; throw "Release check FAILED. Nothing deployed." }
Write-Host "  $(@($r.out)[-1])" -ForegroundColor Green

if ($Deploy) {
    $plugins = Join-Path $Deploy 'plugins'
    if (-not (Test-Path (Join-Path $Deploy 'GTAIV.exe'))) { throw "No GTAIV.exe in $Deploy" }
    Copy-Item $sis $plugins -Force
    $iniSrc = Join-Path $root 'SISCO.ini'
    $iniDst = Join-Path $plugins 'SISCO.ini'
    if ((Test-Path $iniSrc) -and -not (Test-Path $iniDst)) { Copy-Item $iniSrc $iniDst }
    $h1 = (Get-FileHash $sis).Hash; $h2 = (Get-FileHash (Join-Path $plugins 'SISCO.asi')).Hash
    if ($h1 -ne $h2) { throw "Deployed copy differs from the build." }
    Write-Host "  SISCO.asi deployed to $plugins ($buildId, SHA256 $($h1.Substring(0,12)))" -ForegroundColor Green
}
