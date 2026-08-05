[CmdletBinding()]
param(
  [string]$Configuration = 'Release',
  [string]$FfmpegRoot = $env:DIGITOR_FFMPEG_ROOT,
  [int]$Iterations = 120
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Root = Split-Path -Parent $PSScriptRoot
$ArtifactDir = Join-Path $Root 'artifacts/windows-long-run-stability'
$BuildDir = Join-Path $Root 'build/windows-long-run-stability'
$Fixture = Join-Path $ArtifactDir 'long-run-fixture.mp4'
New-Item -ItemType Directory -Force -Path $ArtifactDir | Out-Null

function Invoke-Logged {
  param([string]$Name,[scriptblock]$Command)
  $log = Join-Path $ArtifactDir "$Name.log"
  $previous = $ErrorActionPreference
  try {
    $ErrorActionPreference = 'Continue'
    & $Command *> $log
    $exit = $LASTEXITCODE
  } finally { $ErrorActionPreference = $previous }
  if ($exit -ne 0) {
    Get-Content $log -Tail 120
    throw "$Name failed with exit code $exit. See $log"
  }
  Write-Host "$Name=PASS (log: $log)"
}

if ($Iterations -lt 4) { throw 'Iterations must be at least 4.' }
if ([string]::IsNullOrWhiteSpace($FfmpegRoot) -or -not (Test-Path $FfmpegRoot)) {
  throw 'DIGITOR_FFMPEG_ROOT must point to the FFmpeg development SDK.'
}
$env:Path = "$(Join-Path $FfmpegRoot 'bin');$env:Path"
if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) { throw 'ffmpeg CLI was not found.' }
if (-not (Get-Command vulkaninfo -ErrorAction SilentlyContinue)) { throw 'vulkaninfo was not found.' }

Push-Location $Root
try {
  $summary = Join-Path $ArtifactDir 'summary.txt'
  'WINDOWS_LONG_RUN_STABILITY_QUALIFICATION=STARTED' | Set-Content $summary
  "UTC_TIMESTAMP=$([DateTime]::UtcNow.ToString('o'))" | Add-Content $summary
  "COMMIT=$(git rev-parse HEAD)" | Add-Content $summary
  "OS=$([System.Environment]::OSVersion.VersionString)" | Add-Content $summary
  "ITERATIONS=$Iterations" | Add-Content $summary

  Invoke-Logged 'vulkaninfo-summary' { vulkaninfo --summary }
  $vulkanLog = Join-Path $ArtifactDir 'vulkaninfo-summary.log'
  if (Select-String -Path $vulkanLog -Pattern 'llvmpipe|SwiftShader|Microsoft Basic Render|CPU' -Quiet) {
    throw 'Software Vulkan implementation detected; physical GPU qualification refused.'
  }

  Invoke-Logged 'generate-fixture' {
    ffmpeg -hide_banner -loglevel error -y `
      -f lavfi -i 'testsrc2=size=320x180:rate=30' `
      -f lavfi -i 'sine=frequency=1000:sample_rate=48000' `
      -t 12 -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest $Fixture
  }

  Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
  Invoke-Logged 'configure-stability' {
    cmake -S tests/windows_long_run_stability -B $BuildDir -A x64 `
      -DDIGITOR_ENGINE_ROOT="$Root" `
      -DDIGITOR_FFMPEG_ROOT="$FfmpegRoot"
  }
  Invoke-Logged 'build-stability' { cmake --build $BuildDir --config $Configuration --parallel }

  $exe = Join-Path $BuildDir "$Configuration/digitor_windows_long_run_stability.exe"
  if (-not (Test-Path $exe)) { throw "Stability executable missing: $exe" }
  Invoke-Logged 'run-stability' { & $exe $Fixture $Iterations }
  $runLog = Join-Path $ArtifactDir 'run-stability.log'
  foreach ($marker in @(
    'SIMULATED_GPU_FAILURE_FAIL_CLOSED=PASS',
    'BACKEND_RECREATION_RECOVERY=PASS',
    'REPEATED_SEEK_DECODE=PASS',
    'MEMORY_GROWTH_BOUNDED=PASS',
    'WINDOWS_LONG_RUN_STABILITY=PASS')) {
    if (-not (Select-String -Path $runLog -SimpleMatch $marker -Quiet)) {
      throw "Required stability marker missing: $marker"
    }
  }

  'SIMULATED_GPU_FAILURE_FAIL_CLOSED=PASS' | Add-Content $summary
  'BACKEND_RECREATION_RECOVERY=PASS' | Add-Content $summary
  'REPEATED_SEEK_DECODE=PASS' | Add-Content $summary
  'MEMORY_GROWTH_BOUNDED=PASS' | Add-Content $summary
  'WINDOWS_LONG_RUN_STABILITY_QUALIFICATION=PASS' | Add-Content $summary
  Get-Content $summary
} catch {
  'WINDOWS_LONG_RUN_STABILITY_QUALIFICATION=FAIL' | Add-Content (Join-Path $ArtifactDir 'summary.txt')
  "DIAGNOSTIC=$($_.Exception.Message)" | Add-Content (Join-Path $ArtifactDir 'summary.txt')
  throw
} finally { Pop-Location }
