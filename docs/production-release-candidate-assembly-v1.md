# Production Release Candidate Assembly v1

This milestone assembles platform SDK packages into one release candidate and prevents a release-ready claim until every required platform is present and evidence is coherent.

Required gates: identical semantic version, source commit and ABI; SBOM and provenance; unique package names and SHA-256 values; non-zero artifacts; consumer link smoke; symbol verification; signing declaration; and real hardware qualification for Windows, Android, macOS and iOS.

A candidate with structurally valid packages but missing signing or hardware evidence is `blocked`, not `ready`. External certificates, notarization, store submission and device lab results must be supplied by the release environment.
