# Apply the v4.9.0 production qualification update

Copy these files into the repository:

- `tests/test_log_wheels_native.cpp`
- `.github/workflows/native-gpu-qualification.yml`
- `docs/log_wheels_v4.9.0_truth_table.md`

Then apply `CMakeLists.txt.patch` at the repository root:

```bash
git apply CMakeLists.txt.patch
```

Build locally:

```bash
cmake -S . -B build \
  -DDIGITOR_ENABLE_FFMPEG=OFF \
  -DDIGITOR_BUILD_TESTS=ON \
  -DDIGITOR_BUILD_EXAMPLES=OFF
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

On Windows multi-config generators, add `--config Release` to build and ctest.

## GitHub Actions

Run **Native GPU Qualification** normally for:

- Windows D3D12 and Vulkan
- macOS Metal
- Android arm64 compile/link

For real Android hardware evidence, configure a self-hosted runner with labels:

```text
self-hosted, linux, arm64, android-gpu
```

Connect an arm64 Android device with ADB, then use **Run workflow** and enable:

```text
run_android_hardware = true
```

The milestone is not Android-hardware-qualified until that job passes.
