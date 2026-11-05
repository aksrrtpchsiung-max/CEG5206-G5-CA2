#!/usr/bin/env python3
"""Save local B attribution data and provenance. Not D's formal experiments."""
import argparse
import csv
import datetime
import hashlib
import json
import platform
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def output(command):
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else "unavailable: " + result.stderr.strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "member_b/build/b_tiling_probe")
    parser.add_argument("--out", type=Path, required=True, help="new output directory")
    parser.add_argument("--nodes", type=int, default=8192)
    parser.add_argument("--degree", type=int, default=8)
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    args.out.mkdir(parents=True, exist_ok=False)
    command = [str(args.binary), str(args.nodes), str(args.degree)]
    manifest = {
        "result_kind": "member_b_local_attribution_only", "generator": "b-fixed-hubs-v1",
        "status": "running", "timestamp": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "command": command, "platform": platform.platform(), "isa": "AArch64 NEON FP32x4",
        "cpu": output(["sysctl", "-n", "machdep.cpu.brand_string"]),
        "physical_cores": output(["sysctl", "-n", "hw.physicalcpu"]),
        "l1d_bytes": output(["sysctl", "-n", "hw.l1dcachesize"]),
        "l2_bytes": output(["sysctl", "-n", "hw.l2cachesize"]),
        "compiler": output(["clang++", "--version"]),
        "build_policy": "verify.py: -std=c++17 -O3 -fno-fast-math -ffp-contract=off; scalar additionally -fno-vectorize -fno-slp-vectorize",
        "build_policy_note": "Assumes default verify.py binary; record custom build separately if --binary is changed.",
        "binary_sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest(),
        "git_commit": output(["git", "-C", ROOT, "rev-parse", "HEAD"]),
        "git_status": output(["git", "-C", ROOT, "status", "--short"]),
        "source_sha256": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in sorted((ROOT / "member_b").glob("**/*"))
                          if p.is_file() and p.suffix in (".cpp", ".hpp", ".py")
                          and not any(part.startswith("build") or part == "out" for part in p.relative_to(ROOT / "member_b").parts)},
        "threads": 1, "affinity": "OS managed; no pinning",
        "frequency_policy": "OS managed; uncontrolled", "cache_state": "warm-start; Y scan after each call",
        "warmups": 5, "repetitions": 10, "batch_calls": 1,
        "limitations": "Fixed variant order; one process; no hardware counters; not final tuning or a project speedup claim.",
    }
    path = args.out / "manifest.json"
    path.write_text(json.dumps(manifest, indent=2) + "\n")
    try:
        with (args.out / "calls.csv").open("w") as stream:
            subprocess.run(command, stdout=stream, check=True)
        with (args.out / "calls.csv").open() as stream:
            rows = list(csv.DictReader(stream))
        if len(rows) != 300:
            raise RuntimeError("expected 30 validated variants x 10 repetitions")
        keys = ["family", "n", "m", "f", "variant", "tile_features"]
        summaries = {tuple(row[k] for k in keys): row for row in rows}
        fields = keys + ["median_seconds", "iqr_seconds", "max_scaled_error"]
        with (args.out / "summary.csv").open("w") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields, extrasaction="ignore", lineterminator="\n")
            writer.writeheader()
            writer.writerows(summaries.values())
        manifest["status"] = "completed"
        manifest["rows"] = len(rows)
    except Exception as exc:
        manifest.update(status="failed", error=str(exc))
        raise
    finally:
        path.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Saved {len(rows)} local diagnostic calls to {args.out}")


if __name__ == "__main__":
    main()
