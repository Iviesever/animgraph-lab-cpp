[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [ValidateSet('Property', 'Fuzz')]
  [string]$Target,
  [ValidateSet('Debug', 'Release')]
  [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$manifest = Get-Content -Raw -LiteralPath (Join-Path $projectRoot 'cmake/AnimGraphSources.cmake')
$sources = [regex]::Matches($manifest, '(?m)^\s+(src/[^\s\)]+\.cpp)\s*$') |
  ForEach-Object { $_.Groups[1].Value }
$targetSource = if ($Target -eq 'Property') { 'tests/property/property.cpp' } else { 'tests/fuzz/fuzz.cpp' }
$profile = if ($Configuration -eq 'Release') { 'tests-release' } else { 'tests-debug' }
$output = if ($Target -eq 'Property') { 'animgraph_property' } else { 'animgraph_fuzz' }

Push-Location $projectRoot
try {
  & mqb run @sources $targetSource --profile $profile --std 23 -j 4 -I include `
    /EHsc /W4 /WX /permissive- /utf-8 -o $output --timings=json
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
  Pop-Location
}
