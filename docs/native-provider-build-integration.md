# Native provider release build integration

The production-native provider gate is now part of the normal release configure path through CMake presets. `CMAKE_PROJECT_TOP_LEVEL_INCLUDES` loads `cmake/NativeProviderTopLevel.cmake`, which defers provider attachment until the `digitor_engine` target exists.

A release configure must provide both:

- `DIGITOR_NATIVE_PLATFORM_PROVIDER_SOURCE`
- `DIGITOR_NATIVE_PLATFORM_PROVIDER_IDENTITY`

Example on Windows:

```powershell
cmake --preset windows-native-release `
  -DDIGITOR_NATIVE_PLATFORM_PROVIDER_SOURCE="$PWD/platform/windows/windows_native_provider.cpp" `
  -DDIGITOR_NATIVE_PLATFORM_PROVIDER_IDENTITY="digitor-windows-native-v1"
cmake --build --preset windows-native-release
ctest --preset windows-native-release
```

Configuration fails when the provider source is missing, the identity is empty, or the engine target cannot be attached. Callback-only providers remain invalid under the source-release readiness validator.

The next implementation unit is the repository-owned Windows provider containing the real Flutter texture registrar, D3D12/Vulkan timeline bindings, hardware encoder session and Vulkan/DXGI synchronization path. Android and Apple providers follow the same package boundary.
