[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string]$Package
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Split-Path -Parent $PSScriptRoot)).Path
$artifactsRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'artifacts'))
$packagePath = (Resolve-Path -LiteralPath $Package).Path
$sidecarPath = "$packagePath.sha256"
$extractRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'artifacts\verification\extracted-win64'))
if (-not $extractRoot.StartsWith($artifactsRoot + [IO.Path]::DirectorySeparatorChar,
                                 [StringComparison]::OrdinalIgnoreCase)) {
  throw "Refusing extraction outside artifacts: $extractRoot"
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
  $shippedTrace = Get-Content -Raw -LiteralPath 'samples\trace\locomotion.trace.json' | ConvertFrom-Json
  $shippedViewer = Get-Content -Raw -LiteralPath 'viewer\animgraph_debugger.html'
  if ($graph.instructions.Count -lt 10 -or $shippedTrace.frames.Count -eq 0 -or
      $shippedViewer -notmatch 'trace-data') {
    throw 'Packaged graph, trace, or viewer failed structural verification.'
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
