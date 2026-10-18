# Builds dist\CeylonDemon-Chessmaster-Setup.exe with MSYS2 UCRT64 gcc.
#   .\build.ps1 -EngineDir <folder with the CeylonDemon 3.3 release .exe files> [-WithAvx2] [-Test]
# -WithAvx2 also packs the AVX2 build (used on AMD CPUs older than Zen 3).
# -Test builds a version that does not ask for administrator rights, for testing
#  against copied folders (setup.exe /quiet /cmdir=... /installdir=...).
param(
  [Parameter(Mandatory = $true)][string]$EngineDir,
  [switch]$WithAvx2,
  [switch]$Test
)

# Failures are caught through $LASTEXITCODE: with 'Stop', Windows PowerShell 5.1
# would abort on any compiler warning written to stderr.
$ErrorActionPreference = 'Continue'
Set-Location $PSScriptRoot
if (Test-Path 'C:\msys64\ucrt64\bin') { $env:PATH = "C:\msys64\ucrt64\bin;$env:PATH" }

# Official CeylonDemon 3.3 release files (SHA256SUMS-3.3.txt).
$engines = @{
  'CeylonDemon-3.3-x86-64-bmi2.exe' = 'DB8C46A789A4D6A3957B11A08F625615C50234277197943D6641577AD48D6AAE'
  'CeylonDemon-3.3-x86-64-avx2.exe' = '8EC9047E059F55D0698F9DE22E9653C6AE0B89AB48DED209C5385F3BF6A1CC81'
}
$wanted = @('CeylonDemon-3.3-x86-64-bmi2.exe')
if ($WithAvx2) { $wanted += 'CeylonDemon-3.3-x86-64-avx2.exe' }

New-Item -ItemType Directory -Force installer\payload, build, dist | Out-Null
foreach ($name in $wanted) {
  $src = Join-Path $EngineDir $name
  $hash = (Get-FileHash $src -Algorithm SHA256).Hash
  if ($hash -ne $engines[$name]) { throw "$name does not match the official 3.3 release (SHA-256 $hash)" }
  Copy-Item $src installer\payload\$name -Force
}

# PolyGlot 1.4.70b with the Chessmaster patch (see polyglot\README.md).
$pgFlags = @('-O2', '-DNDEBUG', '-std=gnu17', '-fcommon', '-include', 'windows.h', '-include', 'direct.h', '-w')
$objs = @()
foreach ($c in Get-ChildItem polyglot\src\*.c) {
  $o = "build\pg_$($c.BaseName).o"
  & gcc @pgFlags -c $c.FullName -o $o
  if ($LASTEXITCODE -ne 0) { throw "PolyGlot: compiling $($c.Name) failed" }
  $objs += $o
}
& gcc -s -static @objs -o installer\payload\polyglot.exe
if ($LASTEXITCODE -ne 0) { throw 'PolyGlot: link failed' }

# Installer.
$rcDefs = @()
if ($WithAvx2) { $rcDefs += '-DWITH_AVX2' }
if ($Test) { $rcDefs += '-DASINVOKER' }
Push-Location installer
& windres @rcDefs -O coff setup.rc -o ..\build\setup_res.o
$rc = $LASTEXITCODE
Pop-Location
if ($rc -ne 0) { throw 'resource compile failed' }

$out = if ($Test) { 'dist\CeylonDemon-Chessmaster-Setup-test.exe' } else { 'dist\CeylonDemon-Chessmaster-Setup.exe' }
$cDefs = @()
if ($Test) { $cDefs += '-DTEST_BUILD' }
& gcc -O2 -Wall @cDefs -municode -mwindows installer\setup.c build\setup_res.o -o $out -s -static `
  -lshell32 -lole32 -luuid -ladvapi32
if ($LASTEXITCODE -ne 0) { throw 'installer build failed' }
$size = [math]::Round((Get-Item $out).Length / 1MB, 1)
$sha = (Get-FileHash $out -Algorithm SHA256).Hash
Write-Output "built $out ($size MB)"
Write-Output "SHA-256 $sha"
