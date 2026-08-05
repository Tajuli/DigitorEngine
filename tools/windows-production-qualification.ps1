[CmdletBinding()]
param(
  [switch]$SkipFullSuite,
  [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Root = Split-Path -Parent $PSScriptRoot
$ArtifactDir = Join-Path $Root 'artifacts/windows-production-qualification'
$FullBuildDir = Join-Path $Root 'build/windows-production'
$D3D12BuildDir = Join-Path $Root 'build/windows-d3d12-qualification'

New-Item -ItemType Directory -Force -Path $ArtifactDir | Out-Null

function Invoke-Logged {
  param(
    [Parameter(Mandatory)] [string]$Name,
    [Parameter(Mandatory)] [scriptblock]$Command
  )

  $log = Join-Path $ArtifactDir "$Name.log"
  $previousErrorActionPreference = $ErrorActionPreference
  try {
    # Windows PowerShell can surface native stderr as NativeCommandError when
    # ErrorActionPreference is Stop, even when the native process exits 0.
    # Qualification must be governed by the actual native exit code, while
    # still preserving warnings and diagnostics in the evidence log.
    $ErrorActionPreference = 'Continue'
    & $Command 2>&1 | Tee-Object -FilePath $log
    $nativeExitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousErrorActionPreference
  }

  if ($nativeExitCode -ne 0) {
    throw "$Name failed with exit code $nativeExitCode. See $log"
  }
}

function Require-Marker {
  param(
    [Parameter(Mandatory)] [string]$Path,
    [Parameter(Mandatory)] [string]$Marker
  )
  if (-not (Select-String -Path $Path -SimpleMatch $Marker -Quiet)) {
    throw "Required qualification marker missing: $Marker"
  }
}

Push-Location $Root
try {
  $summary = Join-Path $ArtifactDir 'summary.txt'
  "WINDOWS_PRODUCTION_QUALIFICATION=STARTED" | Set-Content $summary
  "UTC_TIMESTAMP=$([DateTime]::UtcNow.ToString('o'))" | Add-Content $summary
  "COMMIT=$(git rev-parse HEAD)" | Add-Content $summary
  "BRANCH=$(git branch --show-current)" | Add-Content $summary
  "OS=$([System.Environment]::OSVersion.VersionString)" | Add-Content $summary
  "PROCESSOR=$env:PROCESSOR_IDENTIFIER" | Add-Content $summary

  Get-CimInstance Win32_VideoController |
    Select-Object Name, AdapterCompatibility, DriverVersion, DriverDate,
      AdapterRAM, PNPDeviceID, VideoProcessor |
    Format-List | Out-File (Join-Path $ArtifactDir 'gpu-inventory.txt')

  # Python-independent protection for the central fake-GPU invariants.
  $backend = Get-Content 'src/gpu/gpu_backend.cpp' -Raw
  $runtime = Get-Content 'src/runtime/unified_real_media_runtime.cpp' -Raw
  if ($backend -notmatch 'create_native_backend\s*\(\s*backend\s*\)') {
    throw 'GPU backend selection does not delegate to create_native_backend'
  }
  if ($backend -match 'create_gpu_backend[\s\S]*?make_unique\s*<\s*DeviceBackend\s*>') {
    throw 'Probe-only DeviceBackend is being returned as a GPU renderer'
  }
  if ($runtime -notmatch 'refuses CPU-resident playback frames' -or
      $runtime -notmatch 'frame\.frame->backend\(\) == DIGITOR_RENDERER_CPU') {
    throw 'Unified runtime CPU-frame rejection contract is missing'
  }
  'GPU_FIRST_SOURCE_CONTRACT=PASS' | Add-Content $summary

  if (-not $SkipFullSuite) {
    Remove-Item -Recurse -Force $FullBuildDir -ErrorAction SilentlyContinue
    Invoke-Logged 'configure-full' {
      cmake -S . -B $FullBuildDir -A x64 `
        -DDIGITOR_BUILD_TESTS=ON `
        -DDIGITOR_BUILD_EXAMPLES=OFF
    }
    Invoke-Logged 'build-full' {
      cmake --build $FullBuildDir --config $Configuration --parallel
    }
    Invoke-Logged 'ctest-full' {
      ctest --test-dir $FullBuildDir -C $Configuration --output-on-failure
    }
    Require-Marker (Join-Path $ArtifactDir 'ctest-full.log') '100% tests passed'
    'FULL_CTEST_SUITE=PASS' | Add-Content $summary
  }

  Remove-Item -Recurse -Force $D3D12BuildDir -ErrorAction SilentlyContinue
  Invoke-Logged 'configure-d3d12-effects' {
    cmake -S tests/windows_d3d12_effects_qualification `
      -B $D3D12BuildDir -A x64 -DCMAKE_BUILD_TYPE=$Configuration
  }
  Invoke-Logged 'build-d3d12-effects' {
    cmake --build $D3D12BuildDir --config $Configuration --parallel 2
  }

  $exe = Join-Path $D3D12BuildDir "$Configuration/digitor_windows_d3d12_effects_qualification.exe"
  if (-not (Test-Path $exe)) {
    throw "D3D12 qualification executable not found: $exe"
  }
  Invoke-Logged 'd3d12-effects' { & $exe }

  $d3dLog = Join-Path $ArtifactDir 'd3d12-effects.log'
  foreach ($marker in @(
    'D3D12_EFFECTS_QUALIFICATION=PASS',
    'ADAPTER_CLASS=HARDWARE',
    'CPU_READBACKS=0',
    'CPU_REUPLOADS=0',
    'FALLBACK_DISPATCHES=0'
  )) {
    Require-Marker $d3dLog $marker
  }

  'D3D12_PHYSICAL_GPU=PASS' | Add-Content $summary
  'WINDOWS_PRODUCTION_QUALIFICATION=PASS' | Add-Content $summary
  Get-Content $summary
} catch {
  "WINDOWS_PRODUCTION_QUALIFICATION=FAIL" | Add-Content (Join-Path $ArtifactDir 'summary.txt')
  "DIAGNOSTIC=$($_.Exception.Message)" | Add-Content (Join-Path $ArtifactDir 'summary.txt')
  throw
} finally {
  Pop-Location
}
