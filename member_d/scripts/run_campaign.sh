#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
OUT="${1:-member_d/evidence/apple-m4/run-20261110}"
CONFIG=member_d/configs/apple-m4
export OMP_DYNAMIC=FALSE OMP_PROC_BIND=FALSE OMP_WAIT_POLICY=ACTIVE KMP_BLOCKTIME=200
export PYTHONPYCACHEPREFIX=/private/tmp/ceg5206-pycache
mkdir -p "$OUT"
for phase in smoke tuning; do
  python3 member_d/scripts/run.py run --config "$CONFIG/$phase.json" --binary build/benchmark --metadata "$CONFIG/hardware.json" --out "$OUT/$phase"
done
python3 member_d/scripts/freeze_parameters.py "$OUT/tuning" --configs "$CONFIG"
for phase in main scaling reuse ablation; do
  python3 member_d/scripts/run.py run --config "$CONFIG/$phase.json" --binary build/benchmark --metadata "$CONFIG/hardware.json" --out "$OUT/$phase"
done
python3 member_d/scripts/run.py run --config "$CONFIG/diagnostics.json" --binary build/benchmark_profile --metadata "$CONFIG/hardware.json" --out "$OUT/diagnostics"
