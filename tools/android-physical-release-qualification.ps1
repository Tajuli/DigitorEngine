[CmdletBinding()]
param(
  [string]$Configuration = 'Release',
  [string]$FfmpegRoot = $env:DIGITOR_FFMPEG_ROOT,
  [string]$AndroidSdkRoot = $env:ANDROID_SDK_ROOT,
  [string]$AndroidNdkRoot = $(if ($env:ANDROID_NDK_HOME) { $env:ANDROID_NDK_HOME } else { $env:ANDROID_NDK_ROOT })
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Root = Split-Path -Parent $PSScriptRoot
$ArtifactDir = Join-Path $Root 'artifacts/android-physical-release-qualification'
$BuildDir = Join-Path $Root 'build/android-physical-release-qualification'
$Fixture = Join-Path $ArtifactDir 'android-qualification.mp4'
$Shader = Join-Path $ArtifactDir 'android-physical-yuv-to-rgba16f.spv'
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
    Get-Content $log -Tail 120
    throw "$Name failed with exit code $exit. See $log"
  }
  Write-Host "$Name=PASS (log: $log)"
}

if (-not (Get-Command adb -ErrorAction SilentlyContinue)) {
  if ([string]::IsNullOrWhiteSpace($AndroidSdkRoot)) {
    $candidate = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
    if (Test-Path $candidate) { $AndroidSdkRoot = $candidate }
  }
  if (-not [string]::IsNullOrWhiteSpace($AndroidSdkRoot)) {
    $env:Path = "$(Join-Path $AndroidSdkRoot 'platform-tools');$env:Path"
  }
}
if (-not (Get-Command adb -ErrorAction SilentlyContinue)) { throw 'adb was not found.' }

$authorized = @(adb devices | Select-String "`tdevice$")
if ($authorized.Count -ne 1) { throw 'Expected exactly one authorized Android device.' }
$serial = (($authorized[0].ToString() -split "`t")[0]).Trim()

if ([string]::IsNullOrWhiteSpace($AndroidSdkRoot)) {
  $candidate = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
  if (Test-Path $candidate) { $AndroidSdkRoot = $candidate }
}
if ([string]::IsNullOrWhiteSpace($AndroidSdkRoot) -or -not (Test-Path $AndroidSdkRoot)) {
  throw 'Android SDK root was not found. Set ANDROID_SDK_ROOT.'
}

if ([string]::IsNullOrWhiteSpace($AndroidNdkRoot) -or -not (Test-Path $AndroidNdkRoot)) {
  $ndkParent = Join-Path $AndroidSdkRoot 'ndk'
  if (Test-Path $ndkParent) {
    $latest = Get-ChildItem $ndkParent -Directory | Sort-Object Name -Descending | Select-Object -First 1
    if ($latest) { $AndroidNdkRoot = $latest.FullName }
  }
}
if ([string]::IsNullOrWhiteSpace($AndroidNdkRoot) -or -not (Test-Path $AndroidNdkRoot)) {
  throw 'Android NDK was not found. Install it with Android Studio SDK Manager or set ANDROID_NDK_HOME/ANDROID_NDK_ROOT.'
}
$Toolchain = Join-Path $AndroidNdkRoot 'build\cmake\android.toolchain.cmake'
if (-not (Test-Path $Toolchain)) { throw "Android NDK CMake toolchain missing: $Toolchain" }

$Glslc = Join-Path $AndroidNdkRoot 'shader-tools\windows-x86_64\glslc.exe'
if (-not (Test-Path $Glslc)) {
  $glslcCommand = Get-Command glslc -ErrorAction SilentlyContinue
  if ($glslcCommand) { $Glslc = $glslcCommand.Source }
}
if ([string]::IsNullOrWhiteSpace($Glslc) -or -not (Test-Path $Glslc)) {
  throw 'glslc was not found in the Android NDK or PATH.'
}

if ([string]::IsNullOrWhiteSpace($FfmpegRoot) -or -not (Test-Path $FfmpegRoot)) {
  throw 'DIGITOR_FFMPEG_ROOT must point to the FFmpeg SDK used for Windows qualification.'
}
$env:Path = "$(Join-Path $FfmpegRoot 'bin');$env:Path"
if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) { throw 'ffmpeg CLI was not found.' }

Push-Location $Root
try {
  $summary = Join-Path $ArtifactDir 'summary.txt'
  'ANDROID_PHYSICAL_RELEASE_QUALIFICATION=STARTED' | Set-Content $summary
  "UTC_TIMESTAMP=$([DateTime]::UtcNow.ToString('o'))" | Add-Content $summary
  "COMMIT=$(git rev-parse HEAD)" | Add-Content $summary
  "ADB_SERIAL=$serial" | Add-Content $summary

  $manufacturer = (adb -s $serial shell getprop ro.product.manufacturer).Trim()
  $model = (adb -s $serial shell getprop ro.product.model).Trim()
  $hardware = (adb -s $serial shell getprop ro.hardware).Trim()
  $sdk = [int]((adb -s $serial shell getprop ro.build.version.sdk).Trim())
  $qemu = (adb -s $serial shell getprop ro.kernel.qemu).Trim()
  $renderer = ((adb -s $serial shell dumpsys SurfaceFlinger 2>$null | Select-String 'GLES|Vulkan|renderer' | Select-Object -First 1).ToString()).Trim()
  $combined = "$model $hardware $renderer $qemu".ToLowerInvariant()
  if ($combined -match 'emulator|goldfish|ranchu|swiftshader|llvmpipe|softpipe') {
    throw 'Software renderer or emulator detected.'
  }
  if ($sdk -lt 26) { throw "Android API $sdk is below the physical zero-copy harness minimum (26)." }
  "MANUFACTURER=$manufacturer" | Add-Content $summary
  "MODEL=$model" | Add-Content $summary
  "HARDWARE=$hardware" | Add-Content $summary
  "SDK=$sdk" | Add-Content $summary
  "SURFACEFLINGER_RENDERER=$renderer" | Add-Content $summary
  'DEVICE_CLASS=PHYSICAL' | Add-Content $summary

  $features = adb -s $serial shell pm list features
  if (-not ($features | Select-String 'android.hardware.vulkan.version')) {
    throw 'Android device does not advertise Vulkan.'
  }

  Invoke-Logged 'generate-real-media-fixture' {
    ffmpeg -hide_banner -loglevel error -y `
      -f lavfi -i 'testsrc2=size=320x180:rate=30' `
      -t 3 -an -c:v libx264 -profile:v baseline -level 3.0 `
      -pix_fmt yuv420p -g 30 -bf 0 -movflags +faststart $Fixture
  }
  Invoke-Logged 'compile-qualification-shader' {
    & $Glslc -fshader-stage=compute `
      (Join-Path $Root 'shaders\vulkan\android_physical_yuv_to_rgba16f.comp') `
      -o $Shader
  }

  Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
  Invoke-Logged 'configure-android-harness' {
    cmake -S tests/android_physical_runtime -B $BuildDir -G Ninja `
      -DCMAKE_TOOLCHAIN_FILE=$Toolchain `
      -DANDROID_ABI=arm64-v8a `
      -DANDROID_PLATFORM=android-31 `
      -DANDROID_STL=c++_static `
      -DCMAKE_BUILD_TYPE=$Configuration `
      -DCMAKE_CXX_SCAN_FOR_MODULES=OFF
  }
  Invoke-Logged 'build-android-harness' {
    cmake --build $BuildDir --parallel
  }

  $exe = Join-Path $BuildDir 'digitor_android_physical_runtime'
  if (-not (Test-Path $exe)) { throw 'Android physical qualification executable was not produced.' }

  $Remote = '/data/local/tmp/digitor-android-qualification'
  adb -s $serial shell "rm -rf $Remote && mkdir -p $Remote" | Out-Null
  Invoke-Logged 'push-android-harness' {
    adb -s $serial push $exe "$Remote/digitor_android_physical_runtime"
    adb -s $serial push $Fixture "$Remote/input.mp4"
    adb -s $serial push $Shader "$Remote/conversion.spv"
    adb -s $serial shell "chmod 755 $Remote/digitor_android_physical_runtime"
  }
  Invoke-Logged 'run-android-physical-runtime' {
    adb -s $serial shell "cd $Remote && ./digitor_android_physical_runtime input.mp4 conversion.spv"
  }

  $runtimeLog = Join-Path $ArtifactDir 'run-android-physical-runtime.log'
  foreach ($marker in @(
      'BACKEND=Vulkan',
      'DECODER_HARDWARE=1',
      'AHARDWAREBUFFER_FROM_MEDIACODEC=1',
      'VULKAN_EXTERNAL_IMPORT=1',
      'GPU_SUBMISSION=1',
      'GPU_TIMESTAMP_VALID=1',
      'ENCODER_HARDWARE=1',
      'ENCODER_SURFACE_STARTED=1',
      'CPU_READBACKS=0',
      'CPU_REUPLOADS=0',
      'FALLBACK_DISPATCHES=0',
      'INTERMEDIATE_READBACKS=0',
      'INTERMEDIATE_REUPLOADS=0',
      'PREVIEW_EXPORT_PARITY=PASS',
      'ANDROID_PHYSICAL_RELEASE_QUALIFICATION=PASS')) {
    if (-not (Select-String -Path $runtimeLog -SimpleMatch $marker -Quiet)) {
      throw "Required Android qualification marker missing: $marker"
    }
  }

  Get-Content $runtimeLog | Add-Content $summary
  'ANDROID_RELEASE_STATUS=PASS' | Add-Content $summary
  Get-Content $summary
} catch {
  'ANDROID_PHYSICAL_RELEASE_QUALIFICATION=FAIL' | Add-Content (Join-Path $ArtifactDir 'summary.txt')
  "DIAGNOSTIC=$($_.Exception.Message)" | Add-Content (Join-Path $ArtifactDir 'summary.txt')
  throw
} finally {
  Pop-Location
}
