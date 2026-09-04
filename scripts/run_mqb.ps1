[CmdletBinding()]
param(
  [ValidateSet('Lab', 'Tests', 'Property', 'Fuzz')]
  [string]$Target = 'Lab',
  [ValidateSet('Debug', 'Release')]
  [string]$Configuration = 'Debug',
  [string[]]$ProgramArguments = @('verify')
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Split-Path -Parent $PSScriptRoot)).Path
$manifest = Get-Content -Raw -LiteralPath (Join-Path $projectRoot 'cmake\AnimGraphSources.cmake')
$librarySources = [regex]::Matches($manifest, '(?m)^\s+(src/[^\s\)]+\.cpp)\s*$') |
  ForEach-Object { $_.Groups[1].Value }
$testSources = [regex]::Matches($manifest, '(?m)^\s+(tests/[^\s\)]+\.cpp)\s*$') |
  ForEach-Object { $_.Groups[1].Value }
$sha = (git -C $projectRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $sha -notmatch '^[0-9a-f]{40}$') {
  throw 'MQB build requires a repository HEAD with a lowercase 40-character Git SHA.'
}
$shaDefine = 'ANIMGRAPH_GIT_SHA="' + $sha + '"'
$profile = if ($Configuration -eq 'Release') { 'release' } else { 'debug' }
$sources = @($librarySources)
$output = 'animgraph_lab'

switch ($Target) {
  'Lab' { $sources += 'src/cli/main.cpp' }
  'Tests' {
    $sources += $testSources
    $output = 'animgraph_tests'
    $profile = if ($Configuration -eq 'Release') { 'tests-release' } else { 'tests-debug' }
    $ProgramArguments = @()
  }
  'Property' {
    $sources += 'tests/property/property.cpp'
    $output = 'animgraph_property'
    $profile = if ($Configuration -eq 'Release') { 'tests-release' } else { 'tests-debug' }
    $ProgramArguments = @()
  }
  'Fuzz' {
    $sources += 'tests/fuzz/fuzz.cpp'
    $output = 'animgraph_fuzz'
    $profile = if ($Configuration -eq 'Release') { 'tests-release' } else { 'tests-debug' }
    $ProgramArguments = @()
  }
}

$arguments = @('run') + $sources + @(
  '--profile', $profile, '--std', '23', '-j', '4', '-I', 'include',
  '-I', 'tests', '/EHsc', '/W4', '/WX', '/permissive-', '/utf-8',
  '-o', $output, '/D', $shaDefine, '/D', 'ANIMGRAPH_REQUIRE_BOUND_SHA=1',
  '--timings=json'
)
if ($ProgramArguments.Count -gt 0) {
  $arguments += '--'
  $arguments += $ProgramArguments
}

Push-Location $projectRoot
try {
  & mqb @arguments
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
  Pop-Location
}
