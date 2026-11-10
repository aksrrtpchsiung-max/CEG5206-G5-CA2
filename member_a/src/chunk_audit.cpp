#include "checks.hpp"
#include <algorithm>

namespace ceg5206::a {
ValidationResult audit_chunk_plan(CsrView g, ChunkPlanView plan) {
    using namespace detail;
    if (auto r = validate_csr(g); !r.ok()) return r;
    if (!plan.max_chunk_edges || plan.row_chunk_ptr.size != g.n + 1)
        return error(Code::invalid_chunk_plan, "invalid Te or row_chunk_ptr length");
    if (auto r = storage(plan.row_chunk_ptr); !r.ok()) return r;
    if (auto r = storage(plan.chunks); !r.ok()) return r;
    const auto* offsets = plan.row_chunk_ptr.data;
    if (offsets[0] != 0 || offsets[g.n] != plan.chunks.size)
        return error(Code::invalid_chunk_plan, "scan endpoints do not cover descriptors");
    std::uint64_t slots = 0;
    for (std::size_t row = 0; row < g.n; ++row) {
        auto edge = g.row_ptr.data[row];
        const auto end = g.row_ptr.data[row + 1], degree = end - edge;
        const auto count = degree / plan.max_chunk_edges + (degree % plan.max_chunk_edges != 0);
        const auto first = offsets[row], last = offsets[row + 1];
        if (last < first || last > plan.chunks.size || last - first != count)
            return error(Code::invalid_chunk_plan, "incorrect row chunk scan", row, first);
        for (auto k = first; k < last; ++k) {
            const auto& c = plan.chunks.data[k];
            const auto next = edge + std::min(plan.max_chunk_edges, end - edge);
            if (c.row != row || c.edge_begin != edge || c.edge_end != next)
                return error(Code::invalid_chunk_plan, "incorrect row or edge range", row, k);
            if (c.partial_slot != (count > 1 ? slots : kNoIndex))
                return error(Code::invalid_chunk_plan, "incorrect partial slot", row, k);
            if (count > 1) ++slots;
            edge = next;
        }
    }
    if (slots != plan.split_chunk_count) return error(Code::invalid_chunk_plan, "incorrect split_chunk_count");
    return {};
}
} // namespace ceg5206::a
