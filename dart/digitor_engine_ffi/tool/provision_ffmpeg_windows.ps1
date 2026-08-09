param(
  [string]$DestinationRoot = (Join-Path $env:LOCALAPPDATA 'DigitorEngine\deps\vcpkg')
)

$ErrorActionPreference = 'Stop'
$version = '20250622.1.0'
$packageId = 'ffmpeg.lgpl'
$packageUrl = "https://api.nuget.org/v3-flatcontainer/$packageId/$version/$packageId.$version.nupkg"
$cacheRoot = Join-Path $env:LOCALAPPDATA 'DigitorEngine\downloads'
$archive = Join-Path $cacheRoot "$packageId.$version.nupkg"
$zipArchive = Join-Path $cacheRoot "$packageId.$version.zip"
$extractRoot = Join-Path $cacheRoot "$packageId.$version"
$sdkRoot = Join-Path $DestinationRoot 'installed\x64-windows'
$requiredHeader = Join-Path $sdkRoot 'include\libavcodec\avcodec.h'
$requiredLib = Join-Path $sdkRoot 'lib\avformat.lib'

if ((Test-Path $requiredHeader) -and (Test-Path $requiredLib)) {
  Write-Host "DigitorEngine FFmpeg SDK already provisioned at $sdkRoot"
  exit 0
}

New-Item -ItemType Directory -Force $cacheRoot | Out-Null
if (-not (Test-Path $archive)) {
  Write-Host "Downloading FFmpeg.LGPL $version"
  Invoke-WebRequest -Uri $packageUrl -OutFile $archive
}

Copy-Item -LiteralPath $archive -Destination $zipArchive -Force
if (Test-Path $extractRoot) {
  Remove-Item -Recurse -Force $extractRoot
}
New-Item -ItemType Directory -Force $extractRoot | Out-Null
Expand-Archive -LiteralPath $zipArchive -DestinationPath $extractRoot -Force

$header = Get-ChildItem -Path $extractRoot -Recurse -File -Filter 'avcodec.h' |
  Where-Object { $_.FullName -match '[\\/]include[\\/]libavcodec[\\/]avcodec\.h$' } |
  Select-Object -First 1
$avformatLib = Get-ChildItem -Path $extractRoot -Recurse -File -Filter 'avformat.lib' | Select-Object -First 1
$runtimeDll = Get-ChildItem -Path $extractRoot -Recurse -File -Filter 'avcodec-*.dll' | Select-Object -First 1

if (-not $header -or -not $avformatLib -or -not $runtimeDll) {
  throw 'FFmpeg.LGPL package does not contain the required headers, Visual Studio import libraries, and runtime DLLs.'
}

$includeRoot = Split-Path (Split-Path $header.FullName -Parent) -Parent
$libRoot = Split-Path $avformatLib.FullName -Parent
$binRoot = Split-Path $runtimeDll.FullName -Parent

if (Test-Path $sdkRoot) {
  Remove-Item -Recurse -Force $sdkRoot
}
New-Item -ItemType Directory -Force (Join-Path $sdkRoot 'include') | Out-Null
New-Item -ItemType Directory -Force (Join-Path $sdkRoot 'lib') | Out-Null
New-Item -ItemType Directory -Force (Join-Path $sdkRoot 'bin') | Out-Null

Copy-Item -Path (Join-Path $includeRoot '*') -Destination (Join-Path $sdkRoot 'include') -Recurse -Force
Copy-Item -Path (Join-Path $libRoot '*.lib') -Destination (Join-Path $sdkRoot 'lib') -Force
Copy-Item -Path (Join-Path $binRoot '*.dll') -Destination (Join-Path $sdkRoot 'bin') -Force

# The current hook checks for the bootstrap executable before checking whether
# the already-provisioned SDK is complete. This marker prevents the obsolete
# vcpkg bootstrap path from running; no vcpkg command is executed when the SDK
# validation above succeeds.
New-Item -ItemType Directory -Force $DestinationRoot | Out-Null
New-Item -ItemType File -Force (Join-Path $DestinationRoot 'vcpkg.exe') | Out-Null

if (-not (Test-Path $requiredHeader) -or -not (Test-Path $requiredLib)) {
  throw "FFmpeg SDK provisioning failed at $sdkRoot"
}

Write-Host "Provisioned FFmpeg.LGPL $version SDK at $sdkRoot"
