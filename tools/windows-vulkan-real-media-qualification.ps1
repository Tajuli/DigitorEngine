[CmdletBinding()]
param(
  [string]$Configuration = 'Release',
  [string]$FfmpegRoot = $env:DIGITOR_FFMPEG_ROOT,
  [switch]$SkipVulkan,
  [switch]$SkipRealMedia
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Root = Split-Path -Parent $PSScriptRoot
$ArtifactDir = Join-Path $Root 'artifacts/windows-vulkan-real-media-qualification'
$BuildDir = Join-Path $Root 'build/windows-vulkan-real-media'
$FixtureDir = Join-Path $ArtifactDir 'fixtures'
New-Item -ItemType Directory -Force -Path $ArtifactDir, $FixtureDir | Out-Null

function Invoke-Logged {
  param([Parameter(Mandatory)][string]$Name,[Parameter(Mandatory)][scriptblock]$Command)
  $log = Join-Path $ArtifactDir "$Name.log"
  $previous = $ErrorActionPreference
  try {
    $ErrorActionPreference = 'Continue'
    & $Command 2>&1 | Tee-Object -FilePath $log
    $exit = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previous
  }
  if ($exit -ne 0) { throw "$Name failed with exit code $exit. See $log" }
}

function Require-Marker {
  param([string]$Path,[string]$Marker)
  if (-not (Select-String -Path $Path -SimpleMatch $Marker -Quiet)) {
    throw "Required marker missing: $Marker in $Path"
  }
}

Push-Location $Root
try {
  $summary = Join-Path $ArtifactDir 'summary.txt'
  "WINDOWS_VULKAN_REAL_MEDIA_QUALIFICATION=STARTED" | Set-Content $summary
  "UTC_TIMESTAMP=$([DateTime]::UtcNow.ToString('o'))" | Add-Content $summary
  "COMMIT=$(git rev-parse HEAD)" | Add-Content $summary
  "OS=$([System.Environment]::OSVersion.VersionString)" | Add-Content $summary

  if (-not $SkipVulkan) {
    $vulkanInfo = Get-Command vulkaninfo -ErrorAction SilentlyContinue
    if (-not $vulkanInfo) { throw 'vulkaninfo was not found. Install the Vulkan SDK/runtime first.' }
    Invoke-Logged 'vulkaninfo-summary' { vulkaninfo --summary }
    $vulkanLog = Join-Path $ArtifactDir 'vulkaninfo-summary.log'
    if (Select-String -Path $vulkanLog -Pattern 'llvmpipe|SwiftShader|Microsoft Basic Render|CPU' -Quiet) {
      throw 'Software Vulkan implementation detected; physical GPU qualification refused.'
    }

    Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
    Invoke-Logged 'configure-vulkan' {
      cmake -S . -B $BuildDir -A x64 -DDIGITOR_BUILD_TESTS=ON -DDIGITOR_BUILD_EXAMPLES=OFF
    }
    Invoke-Logged 'build-vulkan' { cmake --build $BuildDir --config $Configuration --parallel }
    $nativeGpuExe = Join-Path $BuildDir "$Configuration/digitor_native_gpu_tests.exe"
    if (-not (Test-Path $nativeGpuExe)) { throw "Native GPU test executable missing: $nativeGpuExe" }
    Invoke-Logged 'native-gpu' { & $nativeGpuExe }
    $nativeLog = Join-Path $ArtifactDir 'native-gpu.log'
    Require-Marker $nativeLog 'DEVICE backend=Vulkan'
    Require-Marker $nativeLog 'NATIVE_CACHE backend=Vulkan'
    Require-Marker $nativeLog 'NATIVE_CONSUMER backend=Vulkan'
    if (Select-String -Path $nativeLog -Pattern 'backend=Vulkan.*status=FAIL|QUALIFICATION SKIP' -Quiet) {
      throw 'Vulkan physical qualification reported FAIL or SKIP.'
    }
    'VULKAN_PHYSICAL_GPU=PASS' | Add-Content $summary
  }

  if (-not $SkipRealMedia) {
    $ffmpeg = Get-Command ffmpeg -ErrorAction SilentlyContinue
    $ffprobe = Get-Command ffprobe -ErrorAction SilentlyContinue
    if (-not $ffmpeg -or -not $ffprobe) { throw 'ffmpeg and ffprobe CLI are required.' }
    if ([string]::IsNullOrWhiteSpace($FfmpegRoot) -or -not (Test-Path $FfmpegRoot)) {
      throw 'DIGITOR_FFMPEG_ROOT must point to an FFmpeg development SDK containing headers and libraries.'
    }

    $cfr = Join-Path $FixtureDir 'digitor-cfr.mp4'
    $vfr = Join-Path $FixtureDir 'digitor-vfr.mp4'
    Invoke-Logged 'generate-cfr' {
      ffmpeg -y -f lavfi -i 'testsrc2=size=1280x720:rate=30' -f lavfi -i 'sine=frequency=1000:sample_rate=48000' -t 4 -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest $cfr
    }
    Invoke-Logged 'generate-vfr' {
      ffmpeg -y -f lavfi -i 'testsrc2=size=640x360:rate=30' -vf "select='not(mod(n,3))',setpts=N/(12*TB)" -vsync vfr -t 4 -c:v libx264 -pix_fmt yuv420p $vfr
    }
    Invoke-Logged 'probe-cfr' { ffprobe -v error -show_streams -show_format $cfr }
    Invoke-Logged 'probe-vfr' { ffprobe -v error -show_streams -show_format $vfr }

    Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
    Invoke-Logged 'configure-real-media' {
      cmake -S . -B $BuildDir -A x64 `
        -DDIGITOR_ENABLE_FFMPEG=ON `
        -DDIGITOR_REQUIRE_FFMPEG=ON `
        -DDIGITOR_FFMPEG_ROOT="$FfmpegRoot" `
        -DDIGITOR_BUILD_TESTS=ON `
        -DDIGITOR_BUILD_EXAMPLES=OFF `
        -DDIGITOR_REAL_MEDIA_FIXTURE="$cfr" `
        -DDIGITOR_VFR_MEDIA_FIXTURE="$vfr"
    }
    Invoke-Logged 'build-real-media' { cmake --build $BuildDir --config $Configuration --parallel }
    Invoke-Logged 'ctest-real-media' {
      ctest --test-dir $BuildDir -C $Configuration -R 'digitor_ffmpeg_real_media|digitor_ffmpeg_vfr_parity|digitor_ffmpeg_export_runtime|digitor_timeline_media_adapter|digitor_unified_real_media_runtime' --output-on-failure
    }
    $realLog = Join-Path $ArtifactDir 'ctest-real-media.log'
    if (Select-String -Path $realLog -Pattern 'Failed|No tests were found' -Quiet) {
      throw 'Real-media qualification failed or FFmpeg tests were not registered.'
    }
    Require-Marker $realLog '100% tests passed'
    'REAL_MEDIA_PIPELINE=PASS' | Add-Content $summary
  }

  'WINDOWS_VULKAN_REAL_MEDIA_QUALIFICATION=PASS' | Add-Content $summary
  Get-Content $summary
} catch {
  "WINDOWS_VULKAN_REAL_MEDIA_QUALIFICATION=FAIL" | Add-Content (Join-Path $ArtifactDir 'summary.txt')
  "DIAGNOSTIC=$($_.Exception.Message)" | Add-Content (Join-Path $ArtifactDir 'summary.txt')
  throw
} finally {
  Pop-Location
}
