# Imported Plugin Release Qualification v1

Imported filters and effects are release-qualified only when the exact installed plugin ID, version and package identity produce matching preview and export output on a physical GPU.

Required evidence includes at least 300 preview frames, 300 export frames, pixel comparison, exact alpha preservation, zero CPU readbacks/uploads/fallbacks, 18,000 soak frames and three device-loss recreation cycles.

Missing physical evidence is `UNQUALIFIED`, not passed. Package or visual-stack mismatch, zero-copy violations, alpha mismatch or color tolerance violations are `FAILED`.

SDR RMSE must not exceed 1/255. HDR RMSE must not exceed 0.0005. Preview and export must use the same plugin package and visual-stack digest.
