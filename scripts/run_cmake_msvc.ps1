[CmdletBinding()]
param(
  [ValidateSet('Debug', 'Release')]
  [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
  throw 'Unable to locate vswhere.exe.'
}
$installation = & $vswhere -latest -products '*' `
  -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
  -property installationPath
$vcvars = Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) {
  throw 'Unable to locate vcvars64.bat.'
}

$preset = if ($Configuration -eq 'Release') { 'msvc-local-release' } else { 'msvc-local-debug' }
$command = "call `"$vcvars`" >nul && cmake --preset $preset && cmake --build --preset $preset && ctest --preset $preset"
& cmd.exe /d /s /c $command
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}
