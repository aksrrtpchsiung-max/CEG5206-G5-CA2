#!/usr/bin/env python3
"""D's single entry: inspect availability, plan, or execute linked real kernels."""
import argparse
import csv
import hashlib
import itertools
import json
import math
import platform
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

VARIANTS = ("scalar", "simd", "row_static", "row_dynamic", "split_row", "edge_major")
ROOT = Path(__file__).resolve().parents[2]


def positive(value, name):
    if type(value) is not int or value <= 0:
        raise ValueError(f"{name} must be a positive integer")
    return value


def read_config(path):
    cfg = json.loads(Path(path).read_text(encoding="utf-8"))
    if cfg.get("contract_version") != "0.1":
        raise ValueError("expected contract_version 0.1")
    if cfg.get("phase") not in ("smoke", "single", "scaling", "tuning", "reuse", "diagnostics", "ablation"):
        raise ValueError("unknown phase")
    for field in ("nodes", "mean_degrees", "features", "threads", "tile_features", "max_chunk_edges"):
        if not isinstance(cfg.get(field), list) or not cfg[field]:
            raise ValueError(f"{field} must be a nonempty list")
        if len(set(cfg[field])) != len(cfg[field]):
            raise ValueError(f"duplicate values in {field}")
        for value in cfg[field]:
            positive(value, field)
    if any(n > 2**32 - 1 for n in cfg["nodes"]):
        raise ValueError("n exceeds contract NodeId limit")
    for field in ("features", "threads", "tile_features"):
        if any(x > 2**32 - 1 for x in cfg[field]):
            raise ValueError(f"{field} exceeds uint32")
    if cfg["warmup"] < 5 or cfg["repetitions"] < 10:
        raise ValueError("need >=5 warmups and >=10 timed repetitions")
    positive(cfg["warmup"], "warmup")
    positive(cfg["repetitions"], "repetitions")
    positive(cfg["memory_budget_bytes"], "memory_budget_bytes")
    if cfg["memory_budget_bytes"] > 2**30:
        raise ValueError("project array budget is at most 1 GiB")
    if not cfg["variants"] or len(set(cfg["variants"])) != len(cfg["variants"]) or any(v not in VARIANTS for v in cfg["variants"]):
        raise ValueError("invalid/duplicate variants")
    if not cfg["families"] or len(set(cfg["families"])) != len(cfg["families"]):
        raise ValueError("invalid/duplicate families")
    for family in cfg["families"]:
        if family not in ("uniform", "skewed"):
            raise ValueError("invalid graph family")
        seeds = cfg["seeds"][family]
        for name in ("graph", "feature_0", "feature_1"):
            value = seeds[name]
            if type(value) is not int or not 0 <= value < 2**64:
                raise ValueError("seed must fit uint64")
        if seeds["feature_0"] == seeds["feature_1"]:
            raise ValueError("two distinct feature seeds required")
    if cfg["phase"] == "single" and cfg["threads"] != [1]:
        raise ValueError("single-thread study requires threads=[1]")
    if cfg["phase"] == "scaling":
        cores = positive(cfg.get("physical_cores"), "physical_cores")
        expected, p = {cores}, 1
        while p <= cores:
            expected.add(p)
            p *= 2
        if set(cfg["threads"]) != expected:
            raise ValueError("scaling threads must be 1,2,4,... plus physical_cores")
    return cfg


def expand(cfg):
    cases = []
    product = itertools.product(cfg["families"], cfg["nodes"], cfg["mean_degrees"],
                                cfg["features"], cfg["variants"], cfg["threads"],
                                cfg["tile_features"], cfg["max_chunk_edges"])
    for family, n, degree, f, variant, threads, tile, chunk in product:
        if variant in ("scalar", "simd", "edge_major") and threads != 1:
            continue
        m = n * degree
        if degree > n - 1:
            raise ValueError("degree exceeds no-self-loop graph capacity")
        if family == "skewed":
            hubs = (n + 99) // 100
            if n < 2 or hubs == n or (m // 2 + hubs - 1) // hubs > n - 1 or ((m - m // 2) + n - hubs - 1) // (n - hubs) > n - 1:
                raise ValueError("skewed group exceeds no-self-loop graph capacity")
        seeds = cfg["seeds"][family]
        case = dict(contract_version="0.1", generator_version="a-v0.1", phase=cfg["phase"],
                    variant=variant, family=family, n=n, m=m, f=f,
                    graph_seed=seeds["graph"], feature_seed_0=seeds["feature_0"],
                    feature_seed_1=seeds["feature_1"], threads=threads,
                    tile_features=tile, max_chunk_edges=chunk,
                    warmup=cfg["warmup"], repetitions=cfg["repetitions"],
                    memory_budget_bytes=cfg["memory_budget_bytes"], batch_min_ms=cfg.get("batch_min_ms", 20))
        if cfg["phase"] == "reuse":
            reuse_counts = cfg.get("reuse_counts", [1, 10, 100])
        else:
            reuse_counts = [None]
        for reuse in reuse_counts:
            record = dict(case)
            if reuse is not None:
                record["reuse_calls"] = positive(reuse, "reuse_calls")
            digest = hashlib.sha256(json.dumps(record, sort_keys=True).encode()).hexdigest()[:16]
            record["case_id"] = digest
            cases.append(record)
    return cases


def availability(binary):
    result = subprocess.run([str(binary), "--list"], check=True, capture_output=True, text=True)
    return {row["variant"]: row["available"] for row in json.loads(result.stdout)}


def write_csv(path, rows):
    if not rows:
        return
    fields = list(dict.fromkeys(key for row in rows for key in row))
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def command(binary, case):
    keys = ("variant", "family", "n", "m", "f", "graph_seed", "feature_seed_0", "feature_seed_1",
            "threads", "tile_features", "max_chunk_edges", "warmup", "repetitions", "memory_budget_bytes")
    return [str(binary)] + [str(case[k]) for k in keys] + [str(case.get("reuse_calls", 1)), str(case.get("batch_min_ms", 20))]


def export_results(out, cases):
    tables = {name: [] for name in ("calls", "validation", "summary", "setup_memory", "degree")}
    for case in cases:
        path = out / "cases" / f"{case['case_id']}.json"
        if not path.exists():
            continue
        result = json.loads(path.read_text())
        base = dict(case, run_id=case["case_id"])
        for name in tables:
            rows = result[name] if isinstance(result[name], list) else [result[name]]
            tables[name].extend(dict(base, **row) for row in rows)
    # Only compare identical inputs and numerical/tile policies. Different variants may have different workspace.
    def key(row):
        return tuple(row[k] for k in ("family", "n", "m", "f", "graph_seed", "feature_seed_0",
                                     "feature_seed_1", "tile_features", "max_chunk_edges")) + (row.get("reuse_calls", 1),)
    scalar = {key(r): r["median_seconds"] for r in tables["summary"] if r["variant"] == "scalar" and r["threads"] == 1}
    single = {(key(r), r["variant"]): r["median_seconds"] for r in tables["summary"] if r["threads"] == 1}
    for row in tables["summary"]:
        t = row["median_seconds"]
        row["scalar_speedup"] = scalar.get(key(row), 0) / t if t > 0 and key(row) in scalar else ""
        k = (key(row), row["variant"])
        row["thread_speedup"] = single[k] / t if t > 0 and k in single else ""
    for name, rows in tables.items():
        write_csv(out / f"{name}.csv", rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("plan", "list", "run"))
    parser.add_argument("--config", type=Path)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/benchmark")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--metadata", type=Path, help="JSON hardware/build record; required for execution")
    args = parser.parse_args()
    binary = args.binary.resolve()
    if args.action == "list":
        print(json.dumps(availability(binary), indent=2))
        return
    if not args.config:
        parser.error("--config required")
    cfg = read_config(args.config)
    cases = expand(cfg)
    plan = dict(status="plan_only_no_measurements", config=cfg, case_count=len(cases), cases=cases)
    if args.action == "plan":
        if args.out:
            args.out.parent.mkdir(parents=True, exist_ok=True)
            args.out.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
        print(f"Plan only: {len(cases)} cases; no kernels executed, no performance results.")
        return
    linked = availability(binary)
    missing = sorted({c["variant"] for c in cases if not linked.get(c["variant"], False)})
    if missing:
        raise ValueError("Implementations not linked: " + ", ".join(missing) + ". No measurements executed.")
    if not args.metadata or not args.out:
        parser.error("run requires --metadata and a fresh --out directory")
    metadata = json.loads(args.metadata.read_text(encoding="utf-8"))
    required = ("cpu", "isa", "physical_cores", "cache_sizes", "compiler", "compiler_flags", "affinity", "frequency_policy", "build_command")
    for key in required:
        if not metadata.get(key) or str(metadata[key]).startswith("TODO"):
            raise ValueError(f"fill hardware/build metadata: {key}")
    positive(metadata["physical_cores"], "metadata physical_cores")
    if max(c["threads"] for c in cases) > metadata["physical_cores"]:
        raise ValueError("thread count exceeds recorded physical cores")
    if cfg["phase"] == "scaling" and cfg["physical_cores"] != metadata["physical_cores"]:
        raise ValueError("scaling physical core count differs from metadata")
    if cfg["phase"] not in ("smoke", "tuning") and cfg.get("parameter_status") != "frozen_after_tuning":
        raise ValueError("formal single/scaling execution requires parameters frozen after actual tuning")
    # Fail rather than mixing new results with an existing experiment directory.
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    (out / "cases").mkdir()
    revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True)
    dirty = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT, capture_output=True, text=True)
    manifest = dict(plan, status="running", started_utc=datetime.now(timezone.utc).isoformat(),
                    metadata=metadata, python=platform.python_version(), platform=platform.platform(),
                    source_commit=revision.stdout.strip(), working_tree_dirty=bool(dirty.stdout.strip()),
                    binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                    result_kind="integration_only" if cfg["phase"] == "smoke" else "measurement")
    (out / "source.patch").write_text(subprocess.run(["git", "diff", "HEAD", "--"], cwd=ROOT, capture_output=True, text=True).stdout)
    manifest["runner_sha256"] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    manifest["environment"] = {k: __import__("os").environ.get(k) for k in ("OMP_DYNAMIC", "OMP_PROC_BIND", "OMP_PLACES", "OMP_WAIT_POLICY", "KMP_BLOCKTIME")}
    manifest_path = out / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    import random
    random.Random(cfg.get("execution_order_seed", 52062026)).shuffle(cases)
    manifest["execution_order"] = [c["case_id"] for c in cases]
    try:
        for index, case in enumerate(cases, 1):
            print(f"[{index}/{len(cases)}] {case['variant']} {case['case_id']}", flush=True)
            power = subprocess.run(["pmset", "-g", "batt"], capture_output=True, text=True) if sys.platform == "darwin" else None
            result = subprocess.run(command(binary, case), capture_output=True, text=True)
            if result.returncode:
                (out / "failure.json").write_text(json.dumps(dict(case=case, stderr=result.stderr, stdout=result.stdout, returncode=result.returncode), indent=2))
                raise RuntimeError(f"case failed: {case['case_id']}; inspect failure.json")
            payload = json.loads(result.stdout)
            if power:
                import re
                percent = re.search(r"(\d+)%;", power.stdout)
                payload["power"] = dict(source="AC" if "AC Power" in power.stdout else "battery", percent=int(percent.group(1)) if percent else None)
            if not all(v["passed"] for v in payload["validation"]) or len(payload["calls"]) != case["repetitions"]:
                raise RuntimeError("invalid runner result")
            if any(not math.isfinite(r["call_seconds"]) or r["call_seconds"] <= 0 for r in payload["calls"]):
                raise RuntimeError("non-positive/non-finite duration; batch support required before measuring this case")
            memory = payload["setup_memory"]
            if memory.get("cpp_allocation_peak_bytes", 0) > case["memory_budget_bytes"]:
                raise RuntimeError("C++ allocation budget exceeded")
            (out / "cases" / f"{case['case_id']}.json").write_text(json.dumps(payload, indent=2) + "\n")
        manifest["status"] = "completed"
    except Exception:
        manifest["status"] = "failed"
        raise
    finally:
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
        export_results(out, cases)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, RuntimeError, OSError, subprocess.SubprocessError, KeyError, TypeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)
