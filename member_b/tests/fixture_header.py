"""Translate A's actual JSON handoff fixture into test-only C++ constants."""
import json
import sys
from pathlib import Path

source, destination = map(Path, sys.argv[1:])
d = json.loads(source.read_text())


def floats(values):
    return ",".join(repr(float(v)) + "f" for v in values)


def ints(values):
    return ",".join(str(v) for v in values)


edges = ",".join("{%d,%d,%sf}" % (e["row"], e["col"], repr(float(e["weight"])))
                 for e in d["oracle_edges"])
chunks = ",".join("{%d,%d,%d,%s}" %
                  (c["row"], c["edge_begin"], c["edge_end"],
                   "ceg5206::kNoIndex" if c["partial_slot"] is None else c["partial_slot"])
                  for c in d["chunk_plan"]["chunks"])
text = f"""// Generated from A's fixture; do not edit or use as production input.
#pragma once
#include <ceg5206/contracts.hpp>
namespace fixture {{
inline ceg5206::CsrGraph graph{{{d['n']}, {{{ints(d['csr']['row_ptr'])}}},
    {{{ints(d['csr']['col_idx'])}}}, {{{floats(d['csr']['values'])}}}}};
inline constexpr ceg5206::FeatureCount f = {d['f']};
inline const std::vector<float> x = {{{floats(sum(d['x'], []))}}};
inline const std::vector<float> expected = {{{floats(sum(d['expected_y'], []))}}};
inline const std::vector<ceg5206::WeightedEdge> edges = {{{edges}}};
inline const std::vector<ceg5206::ChunkDescriptor> chunks = {{{chunks}}};
inline const std::vector<float> partials = {{{floats(sum(d['expected_partials'], []))}}};
}}
"""
destination.parent.mkdir(parents=True, exist_ok=True)
destination.write_text(text)
