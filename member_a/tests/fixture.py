"""Translate the shared handoff JSON to C++ test data at build time."""
import json
import sys
from pathlib import Path

source, out = map(Path, sys.argv[1:])
case = json.loads(source.read_text(encoding="utf-8"))
csr, plan = case["csr"], case["chunk_plan"]


def values(seq):
    return "{" + ", ".join(str(x) for x in seq) + "}"


def flatten(matrix):
    return [x for row in matrix for x in row]


edges = values(values((e["row"], e["col"], e["weight"])) for e in case["oracle_edges"])
chunks = values(values((c["row"], c["edge_begin"], c["edge_end"],
                        "kNoIndex" if c["partial_slot"] is None else c["partial_slot"]))
                for c in plan["chunks"])
out.write_text(f"""// Generated from {source.name}
#pragma once
#include <ceg5206/contracts.hpp>
namespace fixture {{
using namespace ceg5206;
inline constexpr auto version = "{case['contract_version']}";
inline constexpr FeatureCount f = {case['f']};
inline const CsrGraph graph{{{case['n']}, {values(csr['row_ptr'])},
    {values(csr['col_idx'])}, {values(csr['values'])}}};
inline const std::vector<WeightedEdge> edges = {edges};
inline const std::vector<float> x = {values(flatten(case['x']))};
inline const std::vector<double> y = {values(flatten(case['expected_y']))};
inline const std::vector<RowOffset> offsets = {values(plan['row_chunk_ptr'])};
inline const std::vector<ChunkDescriptor> chunks = {chunks};
inline constexpr RowOffset te = {plan['max_chunk_edges']};
inline constexpr RowOffset split_count = {plan['split_chunk_count']};
inline const std::vector<float> partials = {values(flatten(case['expected_partials']))};
}}
""", encoding="utf-8")
