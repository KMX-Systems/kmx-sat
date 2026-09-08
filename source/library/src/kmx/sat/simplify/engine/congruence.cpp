/// @file library/src/kmx/sat/simplify/engine/congruence.cpp
/// @brief Out-of-line definitions declared by kmx/sat/simplify/engine/congruence.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/simplify/engine/congruence.hpp>

namespace kmx::sat::simplify::engine
{
    void congruence::run() noexcept
    {
        gate_.materialize_gate_summary();
        apply_gate_constraints();
        derive_equivalences();
        equivalence_substitutor_.rewrite_clauses();
        equivalence_substitutor_.rewrite_watches();
        equivalence_substitutor_.rewrite_external_mapping();
        run_completed_ = true;
    }

    void congruence::derive_equivalences() noexcept
    {
        equivalence_count_ = 0u;
        for (const auto& record: gate_.gate_records())
        {
            if ((record.kind == extractor::gate::gate_kind::xor_gate) || (record.kind == extractor::gate::gate_kind::ite_gate))
                continue;

            if (record.inputs[0u] != record.inputs[1u])
                continue;

            const auto representative = std::min(record.inputs[0u], record.inputs[1u]);
            if ((representative == 0u) || (record.output == 0u) || (record.output == representative))
                continue;

            equivalence_substitutor_.apply_equivalence_class(record.output, representative);
            ++equivalence_count_;
        }
    }
}
