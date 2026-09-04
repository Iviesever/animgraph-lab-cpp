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

$diskLibrarySources = Get-ChildItem -File -Recurse -LiteralPath (Join-Path $projectRoot 'src') -Filter '*.cpp' |
  Where-Object { $_.FullName -notmatch '[\\/](cli|tools)[\\/]' } |
  ForEach-Object { [IO.Path]::GetRelativePath($projectRoot, $_.FullName).Replace('\','/') } |
  Sort-Object -Unique
$diskDifference = Compare-Object -ReferenceObject $cmakeSources -DifferenceObject $diskLibrarySources
if ($diskDifference) {
  $diskDifference | Format-Table -AutoSize
  throw 'The authoritative library manifest differs from source files on disk.'
}

$declaredTests = [regex]::Matches($cmakeText, '(?m)^\s+(tests/[^\s\)]+\.cpp)\s*$') |
  ForEach-Object { $_.Groups[1].Value } |
  Sort-Object -Unique
$diskTests = Get-ChildItem -File -LiteralPath (Join-Path $projectRoot 'tests') -Filter '*.cpp' |
  ForEach-Object { [IO.Path]::GetRelativePath($projectRoot, $_.FullName).Replace('\','/') } |
  Sort-Object -Unique
$testDifference = Compare-Object -ReferenceObject $declaredTests -DifferenceObject $diskTests
if ($testDifference) {
  $testDifference | Format-Table -AutoSize
  throw 'The authoritative unit/integration test manifest differs from disk.'
}

$cmakeLists = Get-Content -Raw -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt')
$entryPoints = @(
  'src/cli/main.cpp','src/tools/animc.cpp',
  'tests/property/property.cpp','tests/fuzz/fuzz.cpp'
)
foreach ($entry in $entryPoints) {
  if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $entry)) -or
      -not $cmakeLists.Contains($entry)) {
    throw "CMake entry point is missing or undeclared: $entry"
  }
}
if ($mqb.build.entry -ne 'src/cli/main.cpp') {
  throw 'MQB CLI entry does not match the authoritative CLI source.'
}

Write-Output "Source manifests match ($($cmakeSources.Count) library, $($declaredTests.Count) unit/integration, 4 entry points)."
