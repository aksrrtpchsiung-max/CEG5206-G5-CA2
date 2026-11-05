#!/usr/bin/env python3
"""Rebuild and verify B without CMake or third-party Python packages (Clang)."""
import argparse
import json
import re
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
B = ROOT / "member_b"


def run(command, **kwargs):
    print("+ " + shlex.join(map(str, command)), flush=True)
    return subprocess.run(list(map(str, command)), cwd=ROOT, check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="clang++", help="Clang-compatible C++ compiler")
    parser.add_argument("--tsan", action="store_true", help="also run ThreadSanitizer separately")
    args = parser.parse_args()
    build = B / "build"
    generated = build / "generated"
    generated.mkdir(parents=True, exist_ok=True)
    run([sys.executable, B / "tests/fixture_header.py",
         ROOT / "member_a/fixtures/contract_v0_1.json", generated / "contract_fixture.hpp"])
    common = [args.cxx, "-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
              "-fno-fast-math", "-ffp-contract=off", "-I", ROOT / "member_a/include"]
    sources = [B / "src/simd.cpp", B / "tests/test_simd.cpp"]
    test_flags = ["-pthread", "-I", generated]
    run(common + ["-O3"] + test_flags + sources + ["-o", build / "test_simd"])
    run([build / "test_simd"])
    run(common + ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        + test_flags + sources + ["-o", build / "test_simd_asan"])
    run([build / "test_simd_asan"])
    if args.tsan:
        run(common + ["-O1", "-g", "-fsanitize=thread", "-fno-omit-frame-pointer"]
            + test_flags + sources + ["-o", build / "test_simd_tsan"])
        run([build / "test_simd_tsan"])

    # Diagnostic controls retain optimization, but scalar arithmetic is not SIMD.
    scalar_flags = ["-fno-vectorize", "-fno-slp-vectorize"]
    run(common + ["-O3"] + scalar_flags + ["-c", B / "bench/scalar.cpp", "-o", build / "scalar.o"])
    run(common + ["-O3", B / "src/simd.cpp", B / "bench/edge_major.cpp", B / "bench/probe.cpp",
                  build / "scalar.o", "-o", build / "b_tiling_probe"])
    for source, name, extra in [(B / "src/simd.cpp", "simd", []),
                                 (B / "bench/scalar.cpp", "scalar", scalar_flags)]:
        run(common + ["-O3"] + extra + ["-S", source, "-o", build / (name + ".s")])
    asm = (build / "simd.s").read_text()
    scalar = (build / "scalar.s").read_text()
    vector = r"\bf(?:mul|add)(?:\.4s\s|\s+v\d+\.4s)"
    fused = r"\bf(?:mla|mls|madd|msub|nmadd|nmsub)(?:\.|\s)"
    if not re.search(vector, asm) or re.search(fused, asm):
        raise RuntimeError("SIMD instructions missing or forbidden FMA present")
    if re.search(vector, scalar) or re.search(fused, scalar):
        raise RuntimeError("scalar control contains vector arithmetic or FMA")
    print("PASS assembly: NEON arithmetic present, no FMA; scalar arithmetic unvectorized", flush=True)

    # Real B linked into D's real runner; a guarded 1024-node A fixture supplies
    # only missing A symbols. This is integration evidence, never a benchmark.
    d_binary = build / "d_b_fixture_runner"
    run(common + ["-O2", "-DD_ENABLE_A", "-DD_ENABLE_B", "-DB_D_TEST_ONLY", "-I",
                  ROOT / "member_d/include", ROOT / "member_d/src/benchmark.cpp",
                  B / "tests/d_fixture_a.cpp", build / "scalar.o", B / "src/simd.cpp", "-o", d_binary])
    with tempfile.TemporaryDirectory(prefix="ceg5206-b-integration-") as temp:
        temp = Path(temp)
        config = json.loads((ROOT / "member_d/configs/smoke.json").read_text())
        config.update(nodes=[1024], mean_degrees=[1], features=[1, 17, 65], families=["uniform"],
                      variants=["scalar", "simd"])
        metadata = {key: "B/D link fixture only, not performance evidence" for key in
                    ("cpu", "isa", "cache_sizes", "compiler", "compiler_flags", "affinity",
                     "frequency_policy", "build_command")}
        metadata["physical_cores"] = 1
        (temp / "config.json").write_text(json.dumps(config))
        (temp / "hardware.json").write_text(json.dumps(metadata))
        run([sys.executable, ROOT / "member_d/scripts/run.py", "run", "--config", temp / "config.json",
             "--binary", d_binary, "--metadata", temp / "hardware.json", "--out", temp / "out"])
        manifest = json.loads((temp / "out/manifest.json").read_text())
        if manifest["status"] != "completed" or manifest["result_kind"] != "integration_only":
            raise RuntimeError("unexpected D manifest")
        cases = list((temp / "out/cases").glob("*.json"))
        if len(cases) != 6:
            raise RuntimeError("expected scalar control and real B at f=1/17/65")
        for path in cases:
            case = json.loads(path.read_text())
            if not all(c["passed"] for c in case["validation"]) or len(case["calls"]) != 10:
                raise RuntimeError("D validation/timing protocol failed")
    print("PASS B linked to D: two inputs, repeated overwrite validation, JSON/CSV export", flush=True)


if __name__ == "__main__":
    main()
