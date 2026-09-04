[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [ValidateSet('Property', 'Fuzz')]
  [string]$Target,
  [ValidateSet('Debug', 'Release')]
  [string]$Configuration = 'Release'
)

& (Join-Path $PSScriptRoot 'run_mqb.ps1') -Target $Target -Configuration $Configuration
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
