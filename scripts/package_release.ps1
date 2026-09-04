[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Split-Path -Parent $PSScriptRoot)).Path
$releaseRoot = Join-Path $projectRoot 'artifacts\release'
$stagingRoot = Join-Path $projectRoot 'artifacts\package-staging\win64'
$releaseExe = Join-Path $projectRoot 'build\msvc-ninja-release\animgraph_lab.exe'
$animcExe = Join-Path $projectRoot 'build\msvc-ninja-release\animc.exe'

if (git -C $projectRoot status --porcelain) {
  throw 'Packaging requires a clean working tree.'
}
if (-not (Test-Path -LiteralPath $releaseExe) -or -not (Test-Path -LiteralPath $animcExe)) {
  throw 'Release executables are missing; run scripts/run_cmake_msvc.ps1 -Configuration Release.'
}
$sha = git -C $projectRoot rev-parse HEAD
$binaryVersion = & $releaseExe --version
if ($LASTEXITCODE -ne 0 -or $binaryVersion -notmatch 'sha=([0-9a-f]{40})' -or $Matches[1] -ne $sha) {
  throw "Release binary does not bind current clean HEAD. binary='$binaryVersion' head='$sha'"
}

function Assert-UnderArtifacts([string]$Path) {
  $artifacts = [IO.Path]::GetFullPath((Join-Path $projectRoot 'artifacts'))
  $resolved = [IO.Path]::GetFullPath($Path)
  if (-not $resolved.StartsWith($artifacts + [IO.Path]::DirectorySeparatorChar,
                                [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing operation outside artifacts: $resolved"
  }
}

Assert-UnderArtifacts $releaseRoot
Assert-UnderArtifacts $stagingRoot
if (Test-Path -LiteralPath $stagingRoot) {
  Remove-Item -Recurse -Force -LiteralPath $stagingRoot
}
New-Item -ItemType Directory -Force -Path $releaseRoot,$stagingRoot | Out-Null

$shortSha = $sha.Substring(0, 8)
$winZip = Join-Path $releaseRoot "AnimGraphLab-Win64-0.1.0-$shortSha.zip"
$sourceZip = Join-Path $releaseRoot "AnimGraphLab-Source-0.1.0-$shortSha.zip"

$directories = 'bin','samples\assets','samples\trace','samples\graph','viewer','docs','reports'
foreach ($directory in $directories) {
  New-Item -ItemType Directory -Force -Path (Join-Path $stagingRoot $directory) | Out-Null
}
Copy-Item -LiteralPath $releaseExe -Destination (Join-Path $stagingRoot 'bin\animgraph_lab.exe')
Copy-Item -LiteralPath $animcExe -Destination (Join-Path $stagingRoot 'bin\animc.exe')
Copy-Item -LiteralPath (Join-Path $projectRoot 'samples\assets\sample.agskel') -Destination (Join-Path $stagingRoot 'samples\assets')
Copy-Item -LiteralPath (Join-Path $projectRoot 'samples\assets\sample.agclip') -Destination (Join-Path $stagingRoot 'samples\assets')
Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination $stagingRoot
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination $stagingRoot
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\PACKAGE_QUICK_START.md') -Destination (Join-Path $stagingRoot 'docs')

Push-Location $stagingRoot
try {
  & '.\bin\animgraph_lab.exe' evaluate --sample locomotion --trace 'samples\trace\locomotion.trace.json'
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  & '.\bin\animgraph_lab.exe' generate-viewer --trace 'samples\trace\locomotion.trace.json' --out 'viewer\animgraph_debugger.html'
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  & '.\bin\animgraph_lab.exe' benchmark --out 'reports\benchmark.json'
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
  Pop-Location
}

$trace = Get-Content -Raw -LiteralPath (Join-Path $stagingRoot 'samples\trace\locomotion.trace.json') | ConvertFrom-Json
$trace.graph_plan | ConvertTo-Json -Depth 20 -Compress |
  Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $stagingRoot 'samples\graph\locomotion.plan.json')

$packagedFiles = Get-ChildItem -File -Recurse -LiteralPath $stagingRoot | Sort-Object FullName | ForEach-Object {
  [ordered]@{
    path = [IO.Path]::GetRelativePath($stagingRoot, $_.FullName).Replace('\','/')
    size = $_.Length
    sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLowerInvariant()
  }
}
$packageManifest = [ordered]@{
  schemaVersion = 1
  product = 'AnimGraphLab'
  version = '0.1.0'
  gitSha = $sha
  platform = 'Win64'
  sourceOnlyGitHubRelease = $true
  files = @($packagedFiles)
}
$packageManifest | ConvertTo-Json -Depth 8 |
  Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $stagingRoot 'MANIFEST.json')

if (Test-Path -LiteralPath $winZip) { Remove-Item -Force -LiteralPath $winZip }
if (Test-Path -LiteralPath $sourceZip) { Remove-Item -Force -LiteralPath $sourceZip }
Compress-Archive -Path (Join-Path $stagingRoot '*') -DestinationPath $winZip -CompressionLevel Optimal
git -C $projectRoot archive --format=zip --output=$sourceZip HEAD
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$artifacts = foreach ($path in $winZip,$sourceZip) {
  $item = Get-Item -LiteralPath $path
  [ordered]@{
    path = [IO.Path]::GetRelativePath($projectRoot, $item.FullName).Replace('\','/')
    size = $item.Length
    sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $item.FullName).Hash.ToLowerInvariant()
  }
}
$delivery = [ordered]@{
  schemaVersion = 1
  product = 'AnimGraphLab'
  version = '0.1.0'
  gitSha = $sha
  generatedUtc = [DateTime]::UtcNow.ToString('o')
  releasePolicy = 'source-only on GitHub; Win64 artifact verified locally only'
  artifacts = @($artifacts)
}
$deliveryPath = Join-Path $releaseRoot 'DELIVERY_MANIFEST.json'
$delivery | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8NoBOM -LiteralPath $deliveryPath
foreach ($artifact in $artifacts) {
  $fileName = [IO.Path]::GetFileName($artifact.path)
  "$($artifact.sha256)  $fileName" |
    Set-Content -Encoding ascii -LiteralPath (Join-Path $releaseRoot "$fileName.sha256")
}

Write-Output ($delivery | ConvertTo-Json -Depth 8 -Compress)
