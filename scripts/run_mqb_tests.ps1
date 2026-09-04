[CmdletBinding()]
param(
  [ValidateSet('Debug', 'Release')]
  [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$manifest = Get-Content -Raw -LiteralPath (Join-Path $projectRoot 'cmake/AnimGraphSources.cmake')
$sources = [regex]::Matches($manifest, '(?m)^\s+((?:src|tests)/[^\s\)]+\.cpp)\s*$') |
  ForEach-Object { $_.Groups[1].Value }
$profile = if ($Configuration -eq 'Release') { 'tests-release' } else { 'tests-debug' }

Push-Location $projectRoot
try {
  & mqb run @sources --profile $profile --std 23 -j 4 -I include -I tests /EHsc /W4 /WX /permissive- /utf-8 --timings=json
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
} finally {
  Pop-Location
}
