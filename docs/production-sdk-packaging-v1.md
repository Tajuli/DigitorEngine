# Production SDK Packaging & Distribution v1

This milestone defines the release gate for DigitorEngine SDK artifacts.

A package is accepted only when its semantic engine version, source commit, ABI version, platform/architecture tuple, required artifacts, SHA-256 values, exported symbol allowlist, release-build policy, separate debug symbols and consumer link smoke result are all valid.

Expected layouts include Windows import/static libraries, Android ABI libraries, Apple framework/XCFramework slices, public headers, license metadata and a machine-readable manifest. Platform signing, notarization and store delivery remain external release operations and must not be claimed by hosted CI alone.

The validator is intentionally independent from payment/licensing and from Flutter UI. It protects distribution correctness and consumer compatibility.