# Plugin runtime safety host flow

1. Build a `PluginRuntimeWorkload` from the already-resolved plugin ID, exact version, SHA-256 package identity and planned GPU workload.
2. Call `PluginRuntimeSafetyController::validate` before dispatching the existing plugin runtime.
3. Surface `PluginRuntimeDiagnostic` through the Digitor app when preview or export cannot continue.
4. Report backend success or failure with `record_backend_result`.
5. Let the app explicitly clear a disabled plugin version after update, reinstall or user recovery.

This layer contains no purchase or entitlement decision and does not replace the existing rendering pipeline.
