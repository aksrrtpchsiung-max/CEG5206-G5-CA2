#!/usr/bin/env python3
"""Select one parameter pair using only the two separate-seed tuning graphs."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path


def select(rows):
    scores = {}
    for row in rows:
        key = (int(row['tile_features']), int(row['max_chunk_edges']))
        scores.setdefault(key, {})[row['family']] = float(row['median_seconds'])
    if len(scores) != 6 or any(set(x) != {'uniform', 'skewed'} for x in scores.values()):
        raise ValueError('need six parameter pairs evaluated on both tuning families')
    output = [dict(tile_features=k[0], max_chunk_edges=k[1], geometric_mean_seconds=math.sqrt(v['uniform'] * v['skewed']),
                   uniform_seconds=v['uniform'], skewed_seconds=v['skewed']) for k, v in scores.items()]
    return sorted(output, key=lambda x: (x['geometric_mean_seconds'], x['tile_features'], x['max_chunk_edges']))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('tuning', type=Path)
    p.add_argument('--configs', type=Path, required=True)
    args = p.parse_args()
    manifest = json.loads((args.tuning / 'manifest.json').read_text())
    if manifest['status'] != 'completed' or manifest['config']['phase'] != 'tuning':
        raise ValueError('tuning must be complete')
    with (args.tuning / 'summary.csv').open() as f:
        scores = select(list(csv.DictReader(f)))
    with (args.tuning / 'parameter_scores.csv').open('w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(scores[0])); w.writeheader(); w.writerows(scores)
    frozen = dict(selection_rule='Minimum geometric mean of full-call medians across the two tuning families',
                  source_manifest_sha256=hashlib.sha256((args.tuning / 'manifest.json').read_bytes()).hexdigest(),
                  selected=scores[0], all_scores=scores)
    (args.configs / 'frozen_parameters.json').write_text(json.dumps(frozen, indent=2) + '\n')
    for path in args.configs.glob('*.json'):
        cfg = json.loads(path.read_text())
        if cfg.get('phase') in ('single', 'scaling', 'reuse', 'ablation', 'diagnostics'):
            cfg.update(parameter_status='frozen_after_tuning', tile_features=[scores[0]['tile_features']],
                       max_chunk_edges=[scores[0]['max_chunk_edges']], tuning_manifest_sha256=frozen['source_manifest_sha256'])
            path.write_text(json.dumps(cfg, indent=2) + '\n')
    print(json.dumps(scores[0], indent=2))


if __name__ == '__main__':
    main()
