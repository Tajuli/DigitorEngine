#!/usr/bin/env bash
set -euo pipefail
command -v adb >/dev/null
count=$(adb devices | awk 'NR>1 && $2=="device" {n++} END {print n+0}')
[ "$count" -eq 1 ] || { echo "Expected exactly one authorized Android device"; exit 2; }
manufacturer=$(adb shell getprop ro.product.manufacturer | tr -d '\r')
model=$(adb shell getprop ro.product.model | tr -d '\r')
hardware=$(adb shell getprop ro.hardware | tr -d '\r')
sdk=$(adb shell getprop ro.build.version.sdk | tr -d '\r')
qemu=$(adb shell getprop ro.kernel.qemu | tr -d '\r')
renderer=$(adb shell dumpsys SurfaceFlinger 2>/dev/null | grep -Eim1 'GLES|Vulkan|renderer' || true)
combined=$(printf '%s %s %s %s' "$model" "$hardware" "$renderer" "$qemu" | tr '[:upper:]' '[:lower:]')
case "$combined" in *emulator*|*goldfish*|*ranchu*|*swiftshader*|*llvmpipe*|*softpipe*) echo "ANDROID_GPU_QUALIFICATION=BLOCKED_SOFTWARE_OR_EMULATOR"; exit 3;; esac
[ -n "$manufacturer" ] && [ -n "$model" ] && [ -n "$hardware" ] && [ "$sdk" -ge 23 ]
printf 'MANUFACTURER=%s\nMODEL=%s\nHARDWARE=%s\nSDK=%s\nRENDERER=%s\n' "$manufacturer" "$model" "$hardware" "$sdk" "$renderer"
echo "DEVICE_CLASS=PHYSICAL"
echo "SOFTWARE_RENDERER=0"
echo "ANDROID_GPU_QUALIFICATION=DEVICE_IDENTITY_PASS"
echo "Runtime GPU submission/timestamp/parity evidence must be supplied by the instrumented DigitorEngine Android harness before release qualification."
