#pragma once
#include <ceg5206/contracts.hpp>
// Local B attribution controls, not implementations of member A's API.
namespace b_probe {
void scalar(ceg5206::CsrView, ceg5206::DenseConstView, ceg5206::DenseView,
            const ceg5206::RunConfig&, ceg5206::KernelState*);
void edge_major(ceg5206::CsrView, ceg5206::DenseConstView, ceg5206::DenseView,
                const ceg5206::RunConfig&, ceg5206::KernelState*);
}
