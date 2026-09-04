[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$cmakeText = Get-Content -Raw -LiteralPath (Join-Path $projectRoot 'cmake/AnimGraphSources.cmake')
$mqb = Get-Content -Raw -LiteralPath (Join-Path $projectRoot 'mqb.json') | ConvertFrom-Json

$cmakeSources = [regex]::Matches($cmakeText, '(?m)^\s+(src/[^\s\)]+\.cpp)\s*$') |
  ForEach-Object { $_.Groups[1].Value } |
  Sort-Object -Unique
$mqbSources = @($mqb.discovery.extra_sources) | Sort-Object -Unique

$difference = Compare-Object -ReferenceObject $cmakeSources -DifferenceObject $mqbSources
if ($difference) {
  $difference | Format-Table -AutoSize
  throw 'CMake and MQB source manifests differ.'
}

Write-Output "Source manifests match ($($cmakeSources.Count) source)."
