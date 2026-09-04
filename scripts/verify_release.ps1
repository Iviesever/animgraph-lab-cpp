[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string]$Package,
  [Parameter(Mandatory)]
  [string]$SourcePackage
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Split-Path -Parent $PSScriptRoot)).Path
$artifactsRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'artifacts'))
$packagePath = (Resolve-Path -LiteralPath $Package).Path
$sidecarPath = "$packagePath.sha256"
$extractRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'artifacts\verification\extracted-win64'))
$sourceExtractRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'artifacts\verification\extracted-source'))
if (-not $extractRoot.StartsWith($artifactsRoot + [IO.Path]::DirectorySeparatorChar,
                                 [StringComparison]::OrdinalIgnoreCase) -or
    -not $sourceExtractRoot.StartsWith($artifactsRoot + [IO.Path]::DirectorySeparatorChar,
                                       [StringComparison]::OrdinalIgnoreCase)) {
  throw 'Refusing extraction outside the project artifacts directory.'
}
if (Test-Path -LiteralPath $extractRoot) {
  Remove-Item -Recurse -Force -LiteralPath $extractRoot
}
New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null
if (-not (Test-Path -LiteralPath $sidecarPath)) {
  throw 'Package SHA-256 sidecar is missing.'
}
$expectedPackageHash = ((Get-Content -Raw -LiteralPath $sidecarPath).Trim() -split '\s+')[0]
$actualPackageHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $packagePath).Hash
if ($actualPackageHash -ne $expectedPackageHash) {
  throw 'Package SHA-256 does not match its sidecar.'
}
Expand-Archive -LiteralPath $packagePath -DestinationPath $extractRoot

Push-Location $extractRoot
try {
  $required = @(
    'bin/animgraph_lab.exe','bin/animc.exe','README.md','LICENSE','docs/PACKAGE_QUICK_START.md',
    'samples/assets/sample.agskel','samples/assets/sample.agclip',
    'samples/graph/locomotion.plan.json','samples/trace/locomotion.trace.json','reports/benchmark.json',
    'viewer/animgraph_debugger.html','MANIFEST.json'
  )
  foreach ($path in $required) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Required package file is missing: $path" }
  }
  $manifest = Get-Content -Raw -LiteralPath 'MANIFEST.json' | ConvertFrom-Json
  if ($manifest.gitSha -notmatch '^[0-9a-f]{40}$') {
    throw 'Package manifest does not contain a full Git SHA.'
  }
  foreach ($entry in $manifest.files) {
    $path = $entry.path.Replace('/', [IO.Path]::DirectorySeparatorChar)
    $item = Get-Item -LiteralPath $path
    if ($item.Length -ne $entry.size) { throw "Manifest size mismatch: $($entry.path)" }
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash
    if ($hash -ne $entry.sha256) { throw "Manifest hash mismatch: $($entry.path)" }
  }
  & '.\bin\animc.exe' validate 'samples\assets\sample.agskel'
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  & '.\bin\animc.exe' validate 'samples\assets\sample.agclip'
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  $graph = Get-Content -Raw -LiteralPath 'samples\graph\locomotion.plan.json' | ConvertFrom-Json
  $shippedTraceText = Get-Content -Raw -LiteralPath 'samples\trace\locomotion.trace.json'
  $shippedTrace = $shippedTraceText | ConvertFrom-Json
  $shippedViewer = Get-Content -Raw -LiteralPath 'viewer\animgraph_debugger.html'
  $shippedBenchmark = Get-Content -Raw -LiteralPath 'reports\benchmark.json' | ConvertFrom-Json
  $embeddedTrace = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($shippedTraceText))
  if ($graph.instructions.Count -lt 10 -or $shippedTrace.frames.Count -eq 0 -or
      $shippedViewer -notmatch 'trace-data' -or -not $shippedViewer.Contains($embeddedTrace) -or
      $shippedTrace.git_sha -ne $manifest.gitSha -or
      $shippedBenchmark.git_sha -ne $manifest.gitSha) {
    throw 'Packaged graph, trace, or viewer failed structural verification.'
  }
  $binaryVersion = & '.\bin\animgraph_lab.exe' --version
  if ($LASTEXITCODE -ne 0 -or $binaryVersion -notmatch 'sha=([0-9a-f]{40})' -or
      $Matches[1] -ne $manifest.gitSha) {
    throw 'Packaged binary, manifest, and generated outputs do not bind the same Git SHA.'
  }
  $animcVersion = & '.\bin\animc.exe' --version
  if ($LASTEXITCODE -ne 0 -or $animcVersion -notmatch 'sha=([0-9a-f]{40})' -or
      $Matches[1] -ne $manifest.gitSha) {
    throw 'Packaged animc does not bind the package Git SHA.'
  }
  & '.\bin\animgraph_lab.exe' verify
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  & '.\bin\animgraph_lab.exe' evaluate --sample locomotion --trace trace.json
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  & '.\bin\animgraph_lab.exe' generate-viewer --trace trace.json --out viewer.html
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  if (-not (Test-Path -LiteralPath 'trace.json') -or -not (Test-Path -LiteralPath 'viewer.html')) {
    throw 'Clean extraction did not generate the required outputs.'
  }
  Write-Output "PACKAGE_VERIFY success trace=$((Get-Item trace.json).Length) viewer=$((Get-Item viewer.html).Length)"
} finally {
  Pop-Location
}

Remove-Item -Recurse -Force -LiteralPath $extractRoot

if ($SourcePackage) {
  $sourcePackagePath = (Resolve-Path -LiteralPath $SourcePackage).Path
  $sourceSidecarPath = "$sourcePackagePath.sha256"
  if (-not (Test-Path -LiteralPath $sourceSidecarPath)) {
    throw 'Source package SHA-256 sidecar is missing.'
  }
  $expectedSourceHash = ((Get-Content -Raw -LiteralPath $sourceSidecarPath).Trim() -split '\s+')[0]
  if ((Get-FileHash -Algorithm SHA256 -LiteralPath $sourcePackagePath).Hash -ne
      $expectedSourceHash) {
    throw 'Source package SHA-256 does not match its sidecar.'
  }
  if (Test-Path -LiteralPath $sourceExtractRoot) {
    Remove-Item -Recurse -Force -LiteralPath $sourceExtractRoot
  }
  New-Item -ItemType Directory -Force -Path $sourceExtractRoot | Out-Null
  Expand-Archive -LiteralPath $sourcePackagePath -DestinationPath $sourceExtractRoot
  Push-Location $sourceExtractRoot
  try {
    foreach ($path in @('CMakeLists.txt','README.md','include','src',
        'samples/trace/locomotion.trace.json','viewer/animgraph_debugger.html',
        'artifacts/reports/benchmark.json','cmake/AnimGraphSourceRevision.cmake',
        'SOURCE_DELIVERY_MANIFEST.json')) {
      if (-not (Test-Path -LiteralPath $path)) { throw "Source package file is missing: $path" }
    }
    $sourceManifest = Get-Content -Raw -LiteralPath 'SOURCE_DELIVERY_MANIFEST.json' | ConvertFrom-Json
    if ($sourceManifest.gitSha -ne $manifest.gitSha -or
        $sourceManifest.generatedBy -notmatch "sha=$($manifest.gitSha)" -or
        $sourceManifest.assetTool -notmatch "sha=$($manifest.gitSha)") {
      throw 'Source package does not bind the Win64 package Git SHA.'
    }
    foreach ($entry in $sourceManifest.generatedFiles) {
      $path = $entry.path.Replace('/', [IO.Path]::DirectorySeparatorChar)
      $item = Get-Item -LiteralPath $path
      if ($item.Length -ne $entry.size -or
          (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash -ne $entry.sha256) {
        throw "Source generated-file hash mismatch: $($entry.path)"
      }
    }
    $sourceTraceText = Get-Content -Raw -LiteralPath 'samples\trace\locomotion.trace.json'
    $sourceTrace = $sourceTraceText | ConvertFrom-Json
    $sourceViewer = Get-Content -Raw -LiteralPath 'viewer\animgraph_debugger.html'
    $sourceBenchmark = Get-Content -Raw -LiteralPath 'artifacts\reports\benchmark.json' | ConvertFrom-Json
    $sourceEmbeddedTrace = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($sourceTraceText))
    if ($sourceTrace.git_sha -ne $sourceManifest.gitSha -or
        $sourceBenchmark.git_sha -ne $sourceManifest.gitSha -or
        -not $sourceViewer.Contains($sourceEmbeddedTrace)) {
      throw 'Source Trace, Viewer, or Benchmark is stale.'
    }
    & pwsh -NoProfile -File '.\scripts\run_cmake_msvc.ps1' -Configuration Release
    if ($LASTEXITCODE -ne 0) { throw 'Source package configure/build/CTest failed.' }
    $sourceBinaryVersion = & '.\build\msvc-ninja-release\animgraph_lab.exe' --version
    $sourceAnimcVersion = & '.\build\msvc-ninja-release\animc.exe' --version
    if ($sourceBinaryVersion -notmatch "sha=$($sourceManifest.gitSha)" -or
        $sourceAnimcVersion -notmatch "sha=$($sourceManifest.gitSha)") {
      throw 'Source package build did not consume its revision metadata.'
    }
    Write-Output "SOURCE_PACKAGE_VERIFY success sha=$($sourceManifest.gitSha)"
  } finally {
    Pop-Location
  }
  Remove-Item -Recurse -Force -LiteralPath $sourceExtractRoot
}
