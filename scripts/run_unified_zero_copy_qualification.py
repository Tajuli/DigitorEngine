#!/usr/bin/env python3
import argparse
import json
import pathlib
import sys
import time

REQUIRED_TRUE = (
    "strict_gpu_first", "decode_zero_copy", "render_zero_copy",
    "preview_export_identity", "per_pixel_accuracy", "hardware_encode",
    "sustained_4k", "stress_and_leak",
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("platform", choices=("windows", "android", "apple"))
    parser.add_argument("--input", required=True, help="Platform qualification JSON")
    parser.add_argument("--output", default="unified-zero-copy-evidence.json")
    args = parser.parse_args()

    source = pathlib.Path(args.input)
    if not source.is_file():
        print(f"qualification report missing: {source}", file=sys.stderr)
        return 2
    report = json.loads(source.read_text(encoding="utf-8"))
    failures = []
    if report.get("platform") != args.platform:
        failures.append("platform mismatch")
    for key in REQUIRED_TRUE:
        if report.get(key) is not True:
            failures.append(f"{key}=false")
    if float(report.get("measured_fps", 0.0)) < float(report.get("minimum_fps", 0.0)):
        failures.append("throughput below minimum")
    if float(report.get("max_mean_error", 1e9)) > float(report.get("allowed_mean_error", -1.0)):
        failures.append("per-pixel error above limit")
    if int(report.get("resource_delta", 1 << 30)) > int(report.get("allowed_resource_delta", -1)):
        failures.append("resource delta above budget")
    for key in ("device_identity", "driver_or_os_build", "engine_commit", "qualification_id"):
        if not report.get(key):
            failures.append(f"missing {key}")

    evidence = dict(report)
    evidence["production_ready"] = not failures
    evidence["failures"] = failures
    evidence["validated_at_unix_seconds"] = int(time.time())
    pathlib.Path(args.output).write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if failures:
        print("qualification rejected: " + "; ".join(failures), file=sys.stderr)
        return 1
    print(f"qualification accepted: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
