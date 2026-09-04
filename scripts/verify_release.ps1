[CmdletBinding()]
param(
  [Parameter(Mandatory)]
  [string]$Package
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Split-Path -Parent $PSScriptRoot)).Path
$artifactsRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'artifacts'))
$packagePath = (Resolve-Path -LiteralPath $Package).Path
$extractRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'artifacts\verification\extracted-win64'))
if (-not $extractRoot.StartsWith($artifactsRoot + [IO.Path]::DirectorySeparatorChar,
                                 [StringComparison]::OrdinalIgnoreCase)) {
  throw "Refusing extraction outside artifacts: $extractRoot"
}
if (Test-Path -LiteralPath $extractRoot) {
  Remove-Item -Recurse -Force -LiteralPath $extractRoot
}
New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null
Expand-Archive -LiteralPath $packagePath -DestinationPath $extractRoot

Push-Location $extractRoot
try {
  & '.\bin\animgraph_lab.exe' verify
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  & '.\bin\animgraph_lab.exe' evaluate --sample locomotion --trace trace.json --git-sha packaged
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
