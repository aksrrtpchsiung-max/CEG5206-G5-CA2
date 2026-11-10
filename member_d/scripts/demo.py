#!/usr/bin/env python3
"""Run a small native correctness and performance demonstration with real kernels."""
import argparse
import csv
import json
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--out',type=Path,required=True)
p.add_argument('--binary',type=Path,default=ROOT/'member_d/build/team/benchmark')
a=p.parse_args()
if a.out.exists():raise SystemExit('Choose a fresh output directory')
a.out.mkdir(parents=True)
cfg=json.loads((ROOT/'member_d/configs/apple-m4/smoke.json').read_text())
frozen=json.loads((ROOT/'member_d/configs/apple-m4/frozen_parameters.json').read_text())['selected']
cfg.update(nodes=[8192],mean_degrees=[32],features=[64],families=['skewed'],variants=['scalar','simd','row_static','split_row'],threads=[1,4],tile_features=[int(frozen['tile_features'])],max_chunk_edges=[int(frozen['max_chunk_edges'])],repetitions=10,batch_min_ms=10)
config=a.out/'demo.json';config.write_text(json.dumps(cfg,indent=2)+'\n')
subprocess.run([sys.executable,str(ROOT/'member_d/scripts/run.py'),'run','--config',str(config),'--binary',str(a.binary.resolve()),'--metadata',str(ROOT/'member_d/configs/apple-m4/hardware.json'),'--out',str(a.out/'results')],check=True)
with (a.out/'results/validation.csv').open() as f:
 checks=list(csv.DictReader(f))
print(f'FP64 validation: {len(checks)} reports, {sum(int(x["failed"]) for x in checks)} failed elements')
print('Variant          Threads  Median ms  Scalar speedup')
with (a.out/'results/summary.csv').open() as f:
 for row in csv.DictReader(f):
  print(f'{row["variant"]:16s} {int(row["threads"]):7d} {float(row["median_seconds"])*1000:10.3f} {float(row["scalar_speedup"]):14.2f}x')
print('Demonstration only. Use the archived full campaign for report conclusions.')
