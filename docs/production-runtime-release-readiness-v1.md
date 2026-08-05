# Production Runtime Integration & Release Readiness v1

This milestone adds an engine-owned runtime session boundary around the existing timeline render orchestrator and production subsystems.

## Runtime responsibilities

- explicit preview and export session kinds
- strict lifecycle: created, preparing, running, cancelling, completed, cancelled or failed
- monotonic frame submission and progress reporting
- CPU/GPU resource-budget enforcement before and after frame execution
- explicit GPU-unavailable failure when GPU execution is required
- cancellation without pretending partially rendered output is complete
- exact failed-frame and failure-status reporting
- optional preview/export digest parity enforcement
- exception containment across progress callbacks and the C ABI

The session manager does not replace transform, color, transitions, compositing, audio or export implementations. It owns production execution safety around those implementations.

## App boundary

Flutter creates a session, displays progress, requests cancellation and consumes snapshots. DigitorEngine owns lifecycle correctness, budgets, failure semantics and parity guards.

## Qualification

The cross-platform qualification executes a 120-frame stress sequence and verifies lifecycle ordering, monotonic progress, backend loss, budget overflow, parity mismatch, cancellation and C ABI behavior under warning-as-error builds.