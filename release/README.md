# Release candidate assembly

Generate one manifest covering Windows, Android, macOS and iOS artifacts. All packages must share the same engine version, source commit and ABI. Attach SBOM and provenance, verify exported symbols and consumer linking, record signing state, and attach real-device qualification evidence. A structurally valid manifest with missing signing or hardware evidence remains blocked.
