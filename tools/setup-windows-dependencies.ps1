[CmdletBinding()]
param(
  [string]$InstallRoot = 'C:\DigitorSDK\ffmpeg',
  [switch]$SkipQualification
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Repository = 'BtbN/FFmpeg-Builds'
$ReleaseApi = "https://api.github.com/repos/$Repository/releases/latest"
$LatestDownloadBase = "https://github.com/$Repository/releases/download/latest"
$PreferredNames = @(
  'ffmpeg-n8.0-latest-win64-gpl-shared-8.0.zip',
  'ffmpeg-master-latest-win64-gpl-shared.zip'
)

function Get-GitHubHeaders {
  $headers = @{ 'User-Agent' = 'DigitorEngine-Dependency-Setup' }
  if (-not [string]::IsNullOrWhiteSpace($env:GITHUB_TOKEN)) {
    $headers['Authorization'] = "Bearer $env:GITHUB_TOKEN"
    $headers['X-GitHub-Api-Version'] = '2022-11-28'
  }
  return $headers
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

function Resolve-Download {
  try {
    $release = Invoke-RestMethod -Uri $ReleaseApi -Headers (Get-GitHubHeaders)
    $assets = @($release.assets)
    foreach ($name in $PreferredNames) {
      $asset = $assets | Where-Object { $_.name -eq $name } | Select-Object -First 1
      if ($asset) {
        $checksumAsset = $assets | Where-Object { $_.name -eq 'checksums.sha256' } | Select-Object -First 1
        if (-not $checksumAsset) { throw 'BtbN checksums.sha256 asset was not found.' }
        return [pscustomobject]@{
          Name = $asset.name
          ArchiveUrl = $asset.browser_download_url
          ChecksumsUrl = $checksumAsset.browser_download_url
          Resolution = 'GitHub API'
        }
      }
    }
    throw 'No preferred BtbN win64 GPL shared FFmpeg SDK asset was found.'
  } catch {
    Write-Warning "GitHub release API unavailable or rate-limited: $($_.Exception.Message)"
    Write-Host 'Falling back to BtbN latest-release download aliases (no API request).'
    return [pscustomobject]@{
      Name = $PreferredNames[0]
      ArchiveUrl = "$LatestDownloadBase/$($PreferredNames[0])"
      ChecksumsUrl = "$LatestDownloadBase/checksums.sha256"
      Resolution = 'latest release alias fallback'
    }
  }
}

$download = Resolve-Download
$temp = Join-Path $env:TEMP ('digitor-ffmpeg-' + [Guid]::NewGuid().ToString('N'))
$zip = Join-Path $temp $download.Name
$checksums = Join-Path $temp 'checksums.sha256'
New-Item -ItemType Directory -Force -Path $temp | Out-Null
try {
  Write-Host "ASSET_RESOLUTION=$($download.Resolution)"
  Write-Host "Downloading $($download.Name)..."
  Invoke-WebRequest -Uri $download.ArchiveUrl -Headers @{ 'User-Agent' = 'DigitorEngine-Dependency-Setup' } -OutFile $zip
  Invoke-WebRequest -Uri $download.ChecksumsUrl -Headers @{ 'User-Agent' = 'DigitorEngine-Dependency-Setup' } -OutFile $checksums

  $expectedLine = Get-Content $checksums | Where-Object { $_ -match [regex]::Escape($download.Name) } | Select-Object -First 1
  if (-not $expectedLine) { throw "Checksum entry missing for $($download.Name)" }
  $expected = ($expectedLine -split '\s+')[0].ToLowerInvariant()
  $actual = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLowerInvariant()
  if ($actual -ne $expected) { throw "SHA256 mismatch for $($download.Name)" }
  Write-Host "SHA256_VERIFIED=$actual"

  $extract = Join-Path $temp 'extract'
  Expand-Archive -Path $zip -DestinationPath $extract -Force
  $sdkRoot = Get-ChildItem $extract -Directory | Where-Object {
    (Test-Path (Join-Path $_.FullName 'bin\ffmpeg.exe')) -and
    (Test-Path (Join-Path $_.FullName 'bin\ffprobe.exe')) -and
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
