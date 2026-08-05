[CmdletBinding()]
param(
  [string]$InstallRoot = 'C:\DigitorSDK\ffmpeg',
  [switch]$SkipQualification
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Invoke-WebJson([string]$Uri) {
  Invoke-RestMethod -Uri $Uri -Headers @{ 'User-Agent' = 'DigitorEngine-Dependency-Setup' }
}

function Add-UserPath([string]$PathToAdd) {
  $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
  $parts = @($userPath -split ';' | Where-Object { $_ })
  if ($parts -notcontains $PathToAdd) {
    [Environment]::SetEnvironmentVariable('Path', (($parts + $PathToAdd) -join ';'), 'User')
  }
  if (($env:Path -split ';') -notcontains $PathToAdd) {
    $env:Path = "$PathToAdd;$env:Path"
  }
}

$release = Invoke-WebJson 'https://api.github.com/repos/BtbN/FFmpeg-Builds/releases/latest'
$assets = @($release.assets)
$preferred = @(
  'ffmpeg-n8.0-latest-win64-gpl-shared-8.0.zip',
  'ffmpeg-master-latest-win64-gpl-shared.zip'
)
$asset = $null
foreach ($name in $preferred) {
  $asset = $assets | Where-Object { $_.name -eq $name } | Select-Object -First 1
  if ($asset) { break }
}
if (-not $asset) {
  $asset = $assets | Where-Object { $_.name -match '^ffmpeg-(n8\.0|master)-latest-win64-gpl-shared.*\.zip$' } | Select-Object -First 1
}
if (-not $asset) { throw 'No BtbN win64 GPL shared FFmpeg SDK asset was found in the latest release.' }

$checksumAsset = $assets | Where-Object { $_.name -eq 'checksums.sha256' } | Select-Object -First 1
if (-not $checksumAsset) { throw 'BtbN checksums.sha256 asset was not found.' }

$temp = Join-Path $env:TEMP ('digitor-ffmpeg-' + [Guid]::NewGuid().ToString('N'))
$zip = Join-Path $temp $asset.name
$checksums = Join-Path $temp 'checksums.sha256'
New-Item -ItemType Directory -Force -Path $temp | Out-Null
try {
  Write-Host "Downloading $($asset.name)..."
  Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zip
  Invoke-WebRequest -Uri $checksumAsset.browser_download_url -OutFile $checksums

  $expectedLine = Get-Content $checksums | Where-Object { $_ -match [regex]::Escape($asset.name) } | Select-Object -First 1
  if (-not $expectedLine) { throw "Checksum entry missing for $($asset.name)" }
  $expected = ($expectedLine -split '\s+')[0].ToLowerInvariant()
  $actual = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLowerInvariant()
  if ($actual -ne $expected) { throw "SHA256 mismatch for $($asset.name)" }

  $extract = Join-Path $temp 'extract'
  Expand-Archive -Path $zip -DestinationPath $extract -Force
  $sdkRoot = Get-ChildItem $extract -Directory | Where-Object {
    (Test-Path (Join-Path $_.FullName 'bin\ffmpeg.exe')) -and
    (Test-Path (Join-Path $_.FullName 'include\libavcodec\avcodec.h')) -and
    (Test-Path (Join-Path $_.FullName 'lib\avcodec.lib'))
  } | Select-Object -First 1
  if (-not $sdkRoot) { throw 'Extracted package does not contain the expected bin/include/lib FFmpeg SDK layout.' }

  if (Test-Path $InstallRoot) { Remove-Item -Recurse -Force $InstallRoot }
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $InstallRoot) | Out-Null
  Move-Item $sdkRoot.FullName $InstallRoot

  [Environment]::SetEnvironmentVariable('DIGITOR_FFMPEG_ROOT', $InstallRoot, 'User')
  $env:DIGITOR_FFMPEG_ROOT = $InstallRoot
  Add-UserPath (Join-Path $InstallRoot 'bin')

  & (Join-Path $InstallRoot 'bin\ffmpeg.exe') -version | Select-Object -First 1
  & (Join-Path $InstallRoot 'bin\ffprobe.exe') -version | Select-Object -First 1
  Write-Host "DIGITOR_FFMPEG_ROOT=$InstallRoot"
  Write-Host 'FFMPEG_WINDOWS_SDK_SETUP=PASS'

  if (-not $SkipQualification) {
    $qualifier = Join-Path (Split-Path -Parent $PSScriptRoot) 'tools\windows-vulkan-real-media-qualification.ps1'
    if (-not (Test-Path $qualifier)) { throw "Qualification script missing: $qualifier" }
    & $qualifier -SkipVulkan -FfmpegRoot $InstallRoot
    if ($LASTEXITCODE -ne 0) { throw "Real-media qualification failed with exit code $LASTEXITCODE" }
  }
} finally {
  Remove-Item -Recurse -Force $temp -ErrorAction SilentlyContinue
}
