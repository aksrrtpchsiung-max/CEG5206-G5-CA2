#!/usr/bin/env python3
"""Run all production kernels on one graph and print their validated timings."""
import argparse
import csv
import json
import os
import platform
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def sysctl(name, fallback):
    if sys.platform != "darwin":
        return fallback
    result = subprocess.run(["sysctl", "-n", name], capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else fallback


def hardware(binary):
    # Read this build's commands; never label a new run with archived M4 metadata.
    commands_file = binary.parent / "compile_commands.json"
    commands = json.loads(commands_file.read_text()) if commands_file.exists() else []
    kernel_commands = [item["command"] for item in commands
                       if Path(item["file"]).name in ("scalar.cpp", "simd.cpp", "contract_adapter.cpp")]
    cache_file = binary.parent / "CMakeCache.txt"
    cache = cache_file.read_text().splitlines() if cache_file.exists() else []
    compiler = next((line.split("=", 1)[1] for line in cache
                     if line.startswith("CMAKE_CXX_COMPILER:FILEPATH=")), "c++")
    version = subprocess.run([compiler, "--version"], capture_output=True, text=True, check=True)
    return {
        "cpu": sysctl("machdep.cpu.brand_string", platform.processor() or platform.machine()),
        "isa": platform.machine(),
        "physical_cores": int(sysctl("hw.physicalcpu", "1")),
        "cache_sizes": "not collected for this demonstration",
        "compiler": version.stdout.splitlines()[0],
        "compiler_flags": kernel_commands or "not recorded; demonstration only",
        "affinity": "OS controlled; OMP_PROC_BIND=FALSE",
        "frequency_policy": "OS controlled; no frequency lock",
        "build_command": "bash member_d/scripts/build_team.sh",
        "os": platform.platform(),
        "notes": ("Demonstration only; not a replacement for the archived experiment campaign. "
                  "If physical core detection is unavailable, use a conservative one-core limit."),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/benchmark")
    args = parser.parse_args()
    binary = args.binary.resolve()
    if not binary.is_file():
        parser.error("Build first: bash member_d/scripts/build_team.sh")
    if args.out.exists():
        parser.error("Choose a fresh output directory, for example results/demo2")

    metadata = hardware(binary)
    config = json.loads((ROOT / "member_d/configs/smoke.json").read_text())
    config.update(nodes=[8192], mean_degrees=[32], features=[64], families=["skewed"],
                  variants=["scalar", "simd", "row_static", "row_dynamic", "split_row"],
                  threads=sorted({1, min(4, metadata["physical_cores"])}), batch_min_ms=10)
    args.out.mkdir(parents=True)
    config_path = args.out / "demo.json"
    metadata_path = args.out / "hardware.json"
    config_path.write_text(json.dumps(config, indent=2) + "\n")
    metadata_path.write_text(json.dumps(metadata, indent=2) + "\n")
    environment = dict(os.environ, OMP_DYNAMIC="FALSE", OMP_PROC_BIND="FALSE",
                       OMP_WAIT_POLICY="ACTIVE", KMP_BLOCKTIME="200")
    subprocess.run([sys.executable, str(ROOT / "member_d/scripts/run.py"), "run",
                    "--config", str(config_path), "--binary", str(binary),
                    "--metadata", str(metadata_path), "--out", str(args.out / "results")],
                   env=environment, check=True)
    with (args.out / "results/validation.csv").open() as stream:
        checks = list(csv.DictReader(stream))
    print(f'FP64 validation: {len(checks)} reports, {sum(int(x["failed"]) for x in checks)} failed elements')
    print("Variant          Threads  Median ms  Scalar speedup")
    with (args.out / "results/summary.csv").open() as stream:
        for row in csv.DictReader(stream):
            print(f'{row["variant"]:16s} {int(row["threads"]):7d} '
                  f'{float(row["median_seconds"]) * 1000:10.3f} '
                  f'{float(row["scalar_speedup"]):14.2f}x')


if __name__ == "__main__":
    main()
