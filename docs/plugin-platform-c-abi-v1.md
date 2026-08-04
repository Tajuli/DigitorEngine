# Plugin Platform C ABI v1

DigitorEngine now exposes a stable pure-C execution boundary for Flutter/Dart FFI and other foreign-language hosts.

## Supported execution

- filter and effect single-input processing
- two-input video transition processing
- preview and export surface metadata
- plugin instance, package version and project/clip identity
- numeric parameter arrays
- zero-copy native texture handles

## ABI stability

Every public request and binding structure includes `struct_size`. Future versions may append fields while version 1 consumers continue to pass the size they were compiled against. `digitor_plugin_platform_c_abi_version()` reports the supported ABI generation.

## Safety

The boundary clears output handles, validates pointers, counts, frame metadata, transition progress and required callbacks, catches C++ exceptions and returns `DigitorResult` values. No C++ STL type crosses the public ABI.

## Commercial policy

The ABI carries preview/export surface metadata only. It does not identify free or paid users and does not decide entitlement. The Digitor app remains responsible for purchase, subscription and export authorization before submitting a request.
