$ErrorActionPreference = 'Stop'

$windowsDir = (Resolve-Path $PSScriptRoot).Path
$repoRoot = Split-Path -Parent $windowsDir
$outputDir = Join-Path $repoRoot 'build-windows'
$distDir = Join-Path $outputDir 'dist'
$releaseDir = Join-Path $outputDir 'release'
$appHeader = Get-Content (Join-Path $windowsDir 'src\app.h') -Raw
$versionMatch = [regex]::Match($appHeader, '(?m)^#define APP_VERSION\s+"([^"]+)"')

if (-not $versionMatch.Success) {
  throw 'Could not read APP_VERSION from src/app.h.'
}
if (-not (Test-Path -LiteralPath (Join-Path $distDir 'Screenator.exe'))) {
  throw 'Run build.bat first; build-windows/dist/Screenator.exe is missing.'
}

$version = $versionMatch.Groups[1].Value
$packageName = "Screenator-$version-win64"
$packageDir = Join-Path $releaseDir $packageName
$zipPath = Join-Path $releaseDir "$packageName.zip"
$releaseFull = [IO.Path]::GetFullPath($releaseDir).TrimEnd('\') + '\'
$packageFull = [IO.Path]::GetFullPath($packageDir)
if (-not $packageFull.StartsWith($releaseFull, [StringComparison]::OrdinalIgnoreCase)) {
  throw "Portable package path is outside the release directory: $packageFull"
}

New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null
if (Test-Path -LiteralPath $packageFull) {
  Remove-Item -LiteralPath $packageFull -Recurse -Force
}
New-Item -ItemType Directory -Path $packageFull | Out-Null
Copy-Item -Path (Join-Path $distDir '*') -Destination $packageFull -Recurse
Compress-Archive -Path $packageFull -DestinationPath $zipPath -CompressionLevel Optimal -Force

Write-Output "Portable package: $packageFull"
Write-Output "Portable archive: $zipPath"
