[CmdletBinding()]
param(
  [ValidateSet('Debug', 'Release')]
  [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$vsRoot = 'C:\Program Files\Microsoft Visual Studio'
$vcvars = Get-ChildItem -LiteralPath $vsRoot -Filter 'vcvars64.bat' -Recurse -File |
  Sort-Object FullName -Descending |
  Select-Object -First 1 -ExpandProperty FullName
if (-not $vcvars) {
  throw 'Unable to locate vcvars64.bat.'
}

$preset = if ($Configuration -eq 'Release') { 'msvc-local-release' } else { 'msvc-local-debug' }
$command = "call `"$vcvars`" >nul && cmake --preset $preset && cmake --build --preset $preset && ctest --preset $preset"
& cmd.exe /d /s /c $command
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}
