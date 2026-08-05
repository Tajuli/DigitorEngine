#!/usr/bin/env python3
"""Fail CI when a GPU-labelled runtime can silently execute on CPU.

This is a source-contract audit, not a hardware qualification replacement.  It
protects the central backend-selection and live-preview invariants on every PR.
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
GPU_BACKEND = ROOT / "src" / "gpu" / "gpu_backend.cpp"
RUNTIME = ROOT / "src" / "runtime" / "unified_real_media_runtime.cpp"
PROVENANCE = ROOT / "src" / "gpu" / "execution_provenance.hpp"


def require(text: str, pattern: str, message: str, failures: list[str]) -> None:
    if re.search(pattern, text, re.MULTILINE | re.DOTALL) is None:
        failures.append(message)


def forbid(text: str, pattern: str, message: str, failures: list[str]) -> None:
    if re.search(pattern, text, re.MULTILINE | re.DOTALL) is not None:
        failures.append(message)


def main() -> int:
    failures: list[str] = []
    backend = GPU_BACKEND.read_text(encoding="utf-8")
    runtime = RUNTIME.read_text(encoding="utf-8")
    provenance = PROVENANCE.read_text(encoding="utf-8")

    # Selection must instantiate a real platform backend. Adapter discovery is
    # informative only and must never produce a renderer that advertises GPU.
    require(
        backend,
        r"create_gpu_backend\s*\([^)]*\).*?create_native_backend\s*\(\s*backend\s*\)",
        "create_gpu_backend must delegate to create_native_backend",
        failures,
    )
    forbid(
        backend,
        r"create_gpu_backend\s*\([^)]*\).*?make_unique\s*<\s*DeviceBackend\s*>",
        "probe-only DeviceBackend must not be returned as an executable GPU backend",
        failures,
    )

    # All live GPU wrappers must reject CPU reference/fallback/readback evidence.
    required_guards = {
        "curves": r"process_curves_gpu.*?cpu_curve_invocations.*?curve_fallback_invocations.*?readback_performed",
        "primary wheels": r"process_primary_wheels_gpu.*?cpu_primary_wheels_invocations.*?primary_wheels_fallback_invocations.*?normal_preview_readback_count",
        "log wheels": r"process_log_wheels_gpu.*?cpu_log_wheels_invocations.*?log_wheels_fallback_invocations.*?normal_preview_readback_count",
        "HSL qualifier": r"process_hsl_qualifier_gpu.*?hsl_qualifier_reference_count\(\).*?normal_preview_readback_count",
    }
    for name, pattern in required_guards.items():
        require(backend, pattern, f"{name} live GPU path lacks silent-CPU/readback rejection", failures)

    # A frame crossing the unified production boundary must be GPU-resident.
    require(
        runtime,
        r"processed->backend\(\)\s*==\s*DIGITOR_RENDERER_CPU.*?throw",
        "unified runtime must reject CPU-resident processed playback frames",
        failures,
    )
    require(
        runtime,
        r"present_for_flutter.*?frame->backend\(\)\s*==\s*DIGITOR_RENDERER_CPU.*?BACKEND_UNAVAILABLE",
        "Flutter presentation boundary must reject CPU frames",
        failures,
    )

    # Provenance must keep explicit counters so tests can distinguish requested
    # GPU from actual GPU execution.
    for field in (
        "gpu_execution",
        "cpu_color_reference_invocations",
        "cpu_fallback_invocations",
        "cpu_curve_invocations",
        "curve_fallback_invocations",
        "cpu_primary_wheels_invocations",
        "primary_wheels_fallback_invocations",
        "cpu_log_wheels_invocations",
        "log_wheels_fallback_invocations",
        "normal_preview_readback_count",
    ):
        require(provenance, rf"\b{re.escape(field)}\b", f"missing execution provenance field: {field}", failures)

    if failures:
        print("GPU-first contract audit FAILED:", file=sys.stderr)
        for item in failures:
            print(f"  - {item}", file=sys.stderr)
        return 1

    print("GPU-first contract audit passed.")
    print("Selection uses native backends; live GPU paths reject CPU fallback/readback evidence.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
