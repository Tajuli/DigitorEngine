[CmdletBinding()]
param(
  [string]$Configuration = 'Release',
  [string]$FfmpegRoot = $env:DIGITOR_FFMPEG_ROOT
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Root = Split-Path -Parent $PSScriptRoot
$ArtifactDir = Join-Path $Root 'artifacts/windows-real-media-preview-export-parity'
$BuildDir = Join-Path $Root 'build/windows-real-media-preview-export-parity'
$Fixture = Join-Path $ArtifactDir 'preview-export-parity.mp4'
New-Item -ItemType Directory -Force -Path $ArtifactDir | Out-Null

function Invoke-Logged {
  param([Parameter(Mandatory)][string]$Name,[Parameter(Mandatory)][scriptblock]$Command)
  $log = Join-Path $ArtifactDir "$Name.log"
  $previous = $ErrorActionPreference
  try {
    $ErrorActionPreference = 'Continue'
    & $Command *> $log
    $exit = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previous
  }
  if ($exit -ne 0) {
    Get-Content $log -Tail 100
    throw "$Name failed with exit code $exit. See $log"
  }
  Write-Host "$Name=PASS (log: $log)"
}

if ([string]::IsNullOrWhiteSpace($FfmpegRoot) -or -not (Test-Path $FfmpegRoot)) {
  throw 'DIGITOR_FFMPEG_ROOT must point to the installed FFmpeg development SDK.'
}
$FfmpegBin = Join-Path $FfmpegRoot 'bin'
$env:Path = "$FfmpegBin;$env:Path"
if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) {
  throw 'ffmpeg CLI was not found.'
}
if (-not (Get-Command vulkaninfo -ErrorAction SilentlyContinue)) {
  throw 'vulkaninfo was not found. Install the Vulkan SDK/runtime first.'
}

Push-Location $Root
try {
  $summary = Join-Path $ArtifactDir 'summary.txt'
  'WINDOWS_REAL_MEDIA_PREVIEW_EXPORT_PARITY=STARTED' | Set-Content $summary
  "UTC_TIMESTAMP=$([DateTime]::UtcNow.ToString('o'))" | Add-Content $summary
  "COMMIT=$(git rev-parse HEAD)" | Add-Content $summary
  "OS=$([System.Environment]::OSVersion.VersionString)" | Add-Content $summary

  Invoke-Logged 'vulkaninfo-summary' { vulkaninfo --summary }
  $vulkanLog = Join-Path $ArtifactDir 'vulkaninfo-summary.log'
  if (Select-String -Path $vulkanLog -Pattern 'llvmpipe|SwiftShader|Microsoft Basic Render|CPU' -Quiet) {
    throw 'Software Vulkan implementation detected; physical GPU qualification refused.'
  }

  Invoke-Logged 'generate-real-media-fixture' {
    ffmpeg -hide_banner -loglevel error -y `
      -f lavfi -i 'testsrc2=size=320x180:rate=30' `
      -f lavfi -i 'sine=frequency=1000:sample_rate=48000' `
      -t 4 -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest $Fixture
  }

  Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
  Invoke-Logged 'configure-parity' {
    cmake -S tests/real_media_preview_export_parity -B $BuildDir -A x64 `
      -DDIGITOR_ENGINE_ROOT="$Root" `
      -DDIGITOR_FFMPEG_ROOT="$FfmpegRoot"
  }
  Invoke-Logged 'build-parity' {
    cmake --build $BuildDir --config $Configuration --parallel
  }

  $exe = Join-Path $BuildDir "$Configuration/digitor_real_media_preview_export_parity.exe"
  if (-not (Test-Path $exe)) { throw "Parity executable missing: $exe" }
  Invoke-Logged 'run-parity' { & $exe $Fixture }
  $parityLog = Join-Path $ArtifactDir 'run-parity.log'
  foreach ($marker in @(
      'PREVIEW_EXPORT_PARITY backend=Vulkan',
      'cpu_invocations=0',
      'fallback_dispatches=0',
      'intermediate_readbacks=0',
      'intermediate_reuploads=0',
      'normal_preview_readbacks=0',
      'REAL_MEDIA_PREVIEW_EXPORT_PARITY=PASS')) {
    if (-not (Select-String -Path $parityLog -SimpleMatch $marker -Quiet)) {
      throw "Required parity marker missing: $marker"
    }
  }

  'REAL_MEDIA_PREVIEW_EXPORT_PARITY=PASS' | Add-Content $summary
  'CPU_INVOCATIONS=0' | Add-Content $summary
  'FALLBACK_DISPATCHES=0' | Add-Content $summary
  'INTERMEDIATE_READBACKS=0' | Add-Content $summary
  'INTERMEDIATE_REUPLOADS=0' | Add-Content $summary
  'NORMAL_PREVIEW_READBACKS=0' | Add-Content $summary
  'WINDOWS_REAL_MEDIA_PREVIEW_EXPORT_PARITY=PASS' | Add-Content $summary
  Get-Content $summary
} catch {
  'WINDOWS_REAL_MEDIA_PREVIEW_EXPORT_PARITY=FAIL' | Add-Content (Join-Path $ArtifactDir 'summary.txt')
  "DIAGNOSTIC=$($_.Exception.Message)" | Add-Content (Join-Path $ArtifactDir 'summary.txt')
  throw
} finally {
  Pop-Location
}
