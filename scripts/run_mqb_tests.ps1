[CmdletBinding()]
param(
  [ValidateSet('Debug', 'Release')]
  [string]$Configuration = 'Debug'
)

& (Join-Path $PSScriptRoot 'run_mqb.ps1') -Target Tests -Configuration $Configuration
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
